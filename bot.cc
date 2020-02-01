
// Simple message bot, does nothing but tell the user a single message

#define _FILE_OFFSET_BITS 64
#include <thread>
#include <regex>
#include <fcgio.h>
#include <json.hpp>
#include <libconfig.h>
#include <signal.h>

#include "httpclient.h"
#include "logger.h"

#define MAX_REQ_SIZE  (4*1024)
#define RET_ERR(x) { std::cerr << x << std::endl; return 1; }

using nlohmann::json;

static std::string escapenl(std::string expr) {
	static std::regex reg("\\n");
	return std::regex_replace(expr, reg, "\\n");
}

bool serving = true;
void sighandler(int) {
	std::cerr << "Signal caught" << std::endl;
	// Just tweak a couple of vars really
	serving = false;
	// Ask for CGI lib shutdown
	FCGX_ShutdownPending();
	// Close stdin so we stop accepting
	close(0);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::cerr << "Usage: " << argv[0] << " file.conf" << std::endl;
		return 1;
	}

	config_t cfg;
	config_init(&cfg);

	if (!config_read_file(&cfg, argv[1]))
		RET_ERR("Error reading config file");

	// Read config vars
	const char *logfile, *tmp_;
	if (!config_lookup_string(&cfg, "logs", &logfile))
		logfile = "/tmp/";

	if (!config_lookup_string(&cfg, "api", &tmp_))
		RET_ERR("Need an API key in the config named 'api'");
	std::string boturlapi = "https://api.telegram.org/bot" + std::string(tmp_);

	if (!config_lookup_string(&cfg, "message", &tmp_))
		RET_ERR("Need to specify a reply message as 'message'");
	std::string reply_message = std::string(tmp_);

	// Start FastCGI interface
	FCGX_Init();

	// Signal handling
	signal(SIGINT, sighandler); 
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, SIG_IGN);

	Logger mainlogger(logfile);

	// Now keep ingesting incoming requests, we do this in the main
	// thread since threads are much slower, unlikely to be a bottleneck.
	while (serving) {
		FCGX_Request request;
		FCGX_InitRequest(&request, 0, 0);

		if (FCGX_Accept_r(&request) >= 0) {
			// Read request
			std::string immresp;
			fcgi_streambuf reqout(request.out);
			std::iostream obuf(&reqout);

			long bsize = atol(FCGX_GetParam("CONTENT_LENGTH", request.envp));
			if (bsize > 0 && bsize < MAX_REQ_SIZE) {
				// Get streams to write
				fcgi_streambuf reqin(request.in);
				std::iostream ibuf(&reqin);

				// Read body and parse JSON
				char body[MAX_REQ_SIZE+1];
				ibuf.read(body, bsize);
				body[bsize] = 0;

				mainlogger.log("INFO Got json request " + escapenl(body));
				try {
					auto req = json::parse(body);
					if (req.count("message") &&
						req["message"].count("text") && req["message"].count("chat")) {

						uint64_t u = req["message"]["chat"]["id"];
						mainlogger.log("INFO Replying user fast " + std::to_string(u));
						immresp = json({
							{"method", "sendMessage"},
							{"chat_id", u},
							{"text", reply_message},
							{"parse_mode", "Markdown"},
						}).dump();
					}
				}
				catch (...) {
					// Return some error maybe?
					mainlogger.log("ERROR Parsing json " + escapenl(body));
				}
			}

			// Respond with an immediate update JSON encoded too
			obuf << "HTTP/1.1 200 OK\r\n"
			     << "Content-Type: application/json\r\n"
			     << "Content-Length: " << immresp.size() << "\r\n\r\n"
			     << immresp;

			FCGX_Finish_r(&request);
		}
	}

	std::cerr << "All clear, service is down" << std::endl;
}

