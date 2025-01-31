#include "../include/HandleRequest.hpp"

LocationConfig findKey(std::string key, int mainKey, ConfigFile &confile) {
	std::map<int, std::map<std::string, LocationConfig>> locations;
	locations = confile.getServerConfig();

    auto mainIt = locations.find(mainKey);
    if (mainIt == locations.end()) {
        throw std::runtime_error("Main key not found");
    }
    std::map<std::string, LocationConfig> &mymap = mainIt->second;
    auto it = mymap.find(key);
	if (it != mymap.end())
        return it->second;
	if (!mymap.empty()){
		mymap.begin()->second.root += key;
		return mymap.begin()->second;
	}
    throw std::runtime_error("Key not found");
}

std::string convertMethod(int method) {
	switch (method) {
		case GET:
			return "GET";
		case POST:
			return "POST";
		case DELETE:
			return "DELETE";
		default:
			return "GET";
	}
}

std::string formPath(std::string_view target, LocationConfig &locationConfig) {
	std::string location;
	std::string targetStr;// (target);
	std::string path;
	size_t pos = target.find('/');
	if (pos == std::string::npos)
		location = "/";
	size_t pos2 = target.find('/', pos + 1);
	if (pos2 == std::string::npos)
	{
		location = "/";
		targetStr = (std::string)target;
	}
	else
	{
	location = (std::string)target.substr(pos, pos2 - pos);
	targetStr = (std::string)target.substr(pos2);
	}
	path = "." + locationConfig.root + targetStr;
	return path;
}

std::string condenceLocation(const std::string_view &input) {
	size_t pos = input.find('/');
	if (pos == std::string::npos)
		return "/";
	size_t pos2 = input.find('/', pos + 1);
	if (pos2 == std::string::npos)
		return "/";
	return (std::string)input.substr(pos, pos2 - pos);
}

bool validateFile(std::string path, HttpResponse &response, LocationConfig &config, int method) { //if the file exists and all is ok
	// Check if the file exists
	if (!std::filesystem::exists(path)) {
		response.setStatusCode(404);
		response.errorPage();
		std::cout << "File not found" << std::endl;
		return false;
	}
	// Check file status
	struct stat filestat;
	if (stat(path.c_str(), &filestat) == -1) {
		response.setStatusCode(500);
		response.errorPage();
		std::cerr << "Error: Unable to stat file " << path << std::endl;
		return false;
	}
	if (config.limit_except.find(convertMethod(method)) == std::string::npos) {
		response.setStatusCode(405);
		std::cout << "Method not allowed" << std::endl;
		response.errorPage();
		return false;
	}
	return true;
}

void handleDelete(HttpParser& request, ConfigFile &confile, int serverIndex, HttpResponse &response) { //deletes a file
	LocationConfig locationConfig = findKey(condenceLocation(request.getTarget()), serverIndex, confile);
	std::string path = formPath(request.getTarget(), locationConfig);
//	std::cout << "path is " << path << std::endl;
	if (response.getStatus() == 404 || response.getStatus() == 405) {
		response.errorPage();
		return;
	}
	if (!validateFile(path, response, locationConfig, DELETE))
		return;
    // Attempt to remove the file
    std::error_code ec;
    if (std::filesystem::remove(path, ec)) {
        response.setStatusCode(204);
        std::cout << "File deleted" << std::endl;
		return;
    } else {
        if (ec) {
            response.setStatusCode(403);
            std::cerr << "Error: " << ec.message() << std::endl;
			response.errorPage();
			return;
        } else {
            response.setStatusCode(500);
            std::cerr << "Internal Server Error" << std::endl;
			response.errorPage();
			return;
        }
    }
}

std::string getExtension(const std::string_view& url) {
	size_t pos = url.find_last_of('.');
	if (pos == std::string::npos)
		return ".html";
	return std::string(url.substr(pos));
}

void listDirectory(HttpParser &request, std::string &path, LocationConfig &location, HttpResponse &response) {
	if (location.autoindex) {
		std::ostringstream buffer;
		buffer << "<!DOCTYPE html>\n<html><head><title>Index of " << request.getTarget() << "</title></head><body><h1>Index of " << request.getTarget() << "</h1><hr><pre>";
		DIR *dir;
		struct dirent *ent;
		if ((dir = opendir(path.c_str())) != NULL) {
			while ((ent = readdir(dir)) != NULL) {
				buffer << "<a href=\"" << ent->d_name << "\">" << ent->d_name << "</a><br>";
			}
			closedir(dir);
		}
		buffer << "</pre><hr></body></html>";
		response.setMimeType(".html");
		return;
	}
	else
	{
		response.setStatusCode(403);
		return;
	}
}

void locateAndReadFile(HttpParser &request, ConfigFile &confile, int serverIndex, HttpResponse &response) {
	LocationConfig location;
	std::string locationStr = condenceLocation(request.getTarget());
	try {
	location = findKey(locationStr, serverIndex, confile);
	}	catch (std::runtime_error &e) {
		response.setStatusCode(500);
		response.errorPage();
		return;
		}
	std::string path;       // = "." + location.root;
	path = formPath(request.getTarget(), location);
	std::string error = confile.getErrorPage(serverIndex);
	if (request.getTarget() == "/")
		path += location.index;
//	std::cout << "path is " << path << std::endl;
	if (!validateFile(path, response, location, GET))
		return;
//	std::cout << "locationstr is " << locationStr << std::endl;
//	std::cout << "path is " << path << std::endl;
	if (locationStr == "/cgi") { //calls the cgi executor
		std::cout << "its cgi! " << std::endl;
		response.createCgi();
		try {
			response.startCgi(path, request.getQuery(), "", GET, response);
			response.setStatusCode(102);
			return;
		}
		catch (std::runtime_error &e) {
			response.errorPage();
			return;
		}

	}
	struct stat fileStat;
	stat(path.c_str(), &fileStat);
	response.setMimeType(getExtension(path));
	if (S_ISDIR(fileStat.st_mode))
		return (listDirectory(request, path, location, response));
	std::ifstream file(path, std::ios::binary);
	if (!file){
		response.setStatusCode(500);
		response.errorPage();
		return;
	}
//	response.setStatusCode(200);
	std::ostringstream buffer;
	buffer << file.rdbuf();
	response.setBody(buffer.str());
	return;
}

void handlePost(HttpParser &request, ConfigFile &confile, int serverIndex, HttpResponse &response) {
	LocationConfig location;
	std::string locationStr = condenceLocation(request.getTarget());
	if (locationStr != "/cgi") {
		response.setStatusCode(400);
		response.setMimeType(".html");
		response.setHeader("Server", confile.getServerName(serverIndex));
		response.setBody("Bad Request");
		return;
	}
	location = findKey(locationStr, serverIndex, confile);
	response.createCgi();
	response.startCgi(location.root, request.getQuery(), request.getBody(), POST, response);
		response.setStatusCode(102);
		return;
}

void receiveRequest(HttpParser& request, ConfigFile &confile, int serverIndex, HttpResponse &response) {
	response.cgidone = false;
	response.setHeader("Server", confile.getServerName(serverIndex));
	response.setErrorpath(confile.getErrorPage(serverIndex));
	unsigned int status = request.getStatus();
	response.setStatusCode(status);
	std::cout << "status is " << status << std::endl;
	if (status != 200 && status != 201 && status != 204)
	{
		std::cout << "status is ll" << status << std::endl;
		response.setStatusCode(status);
		response.errorPage();
//		response.setBody("Bad Request");
		return;
	}
	switch (request.getMethod()) {
		case DELETE:
			response.setStatusCode(status);
			handleDelete(request, confile, serverIndex, response);
		//	response.setBody("Not found");
			return;
		case GET:
	//		std::cout << "entering response with status :" << response.getStatus() << std::endl;
			locateAndReadFile(request, confile, serverIndex, response);
	//		std::cout << "child id in receieve request is " << response.getchildid() << std::endl;;
	//		std::cout << "returning from response with status :" << response.getStatus() << std::endl;
			return;
		case POST:
			response.setHeader("Server", confile.getServerName(serverIndex));
			if (status == 201) {
				response.setStatusCode(status);
				response.setMimeType(".txt");
				response.setBody("Created");
			}
			else
				handlePost(request, confile, serverIndex, response);
			return;
		default:
			response.setStatusCode(405);
			response.errorPage();
		//	response.setBody("Not found");
			return;
	}
}
