#include "rest_server.h"

#define PCI_PRI_FMT "%.4" PRIx16 ":%.2" PRIx8 ":%.2" PRIx8 ".%" PRIx8

GraphManager *RestServer::gm = NULL;
SQLiteManager *dbmanager = NULL;

SecurityManager *secmanager = NULL;

bool client_auth = false;

bool RestServer::init(SQLiteManager *dbm, bool cli_auth, char *nffg_filename,
		int core_mask, char *ports_file_name, string un_address,
		bool orchestrator_in_band, char *un_interface,
		char *ipsec_certificate) {
	char *nffg_file_name = new char[BUFFER_SIZE];
	if (nffg_filename != NULL && strcmp(nffg_filename, "") != 0)
		strcpy(nffg_file_name, nffg_filename);
	else
		nffg_file_name = NULL;

	try {
		gm = new GraphManager(core_mask, string(ports_file_name), un_address,
				orchestrator_in_band, string(un_interface),
				string(ipsec_certificate));

	} catch (...) {
		return false;
	}

	//Handle the file containing the first graph to be deployed
	if (nffg_file_name != NULL) {
		sleep(2); //XXX This give time to the controller to be initialized

		if (!readGraphFromFile(nffg_file_name)) {
			delete gm;
			return false;
		}
	}

	client_auth = cli_auth;

	if (client_auth) {
		dbmanager = dbm;
		dbmanager->cleanTables();
		secmanager = new SecurityManager(dbmanager);
	}

	return true;
}

bool RestServer::readGraphFromFile(char *nffg_filename) {
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
			"Considering the graph described in file '%s'", nffg_filename);

	std::ifstream file;
	file.open(nffg_filename);
	if (file.fail()) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__,
				"Cannot open the file %s", nffg_filename);
		return false;
	}

	stringstream stream;
	string str;
	while (std::getline(file, str))
		stream << str << endl;

	if (createGraphFromFile(stream.str()) == 0)
		return false;

	return true;
}

void RestServer::terminate() {
	delete (gm);
}

bool RestServer::isLoginRequest(const char *method, const char *url) {
	/*
	 * Checking method name and url is enough because the REST server
	 * already verifies that the request is well-formed.
	 */
	return (strcmp(method, POST) == 0 && url[0] == '/'
			&& strncmp(url + sizeof(char), BASE_URL_LOGIN,
					sizeof(char) * strlen(BASE_URL_LOGIN)) == 0);
}

void RestServer::request_completed(void *cls, struct MHD_Connection *connection,
		void **con_cls, enum MHD_RequestTerminationCode toe) {
	struct connection_info_struct *con_info =
			(struct connection_info_struct *) (*con_cls);

	if (NULL == con_info)
		return;

	if (con_info->length != 0) {
		free(con_info->message);
		con_info->message = NULL;
	}

	free(con_info);
	*con_cls = NULL;
}

int RestServer::answer_to_connection(void *cls,
		struct MHD_Connection *connection, const char *url, const char *method,
		const char *version, const char *upload_data, size_t *upload_data_size,
		void **con_cls) {

	if (NULL == *con_cls) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "New %s request for %s using version %s", method, url, version);

		if (LOGGING_LEVEL <= ORCH_DEBUG)
			MHD_get_connection_values(connection, MHD_HEADER_KIND, &print_out_key, NULL);

		struct connection_info_struct *con_info;
		con_info = (struct connection_info_struct*) malloc(
				sizeof(struct connection_info_struct));

		assert(con_info != NULL);
		if (NULL == con_info)
			return MHD_NO;

		if ((0 == strcmp(method, PUT)) || (0 == strcmp(method, POST))
				|| (0 == strcmp(method, DELETE))) {
			con_info->message = (char*) malloc(REQ_SIZE * sizeof(char));
			con_info->length = 0;
		} else if (0 == strcmp(method, GET))
			con_info->length = 0;
		else {
			con_info->message = (char*) malloc(REQ_SIZE * sizeof(char));
			con_info->length = 0;
		}

		*con_cls = (void*) con_info;
		return MHD_YES;
	}

	// Process request

	if (0 == strcmp(method, GET))
		return doOperation(connection, con_cls, GET, url);

	if ((0 == strcmp(method, PUT)) || (0 == strcmp(method, POST))
			|| (0 == strcmp(method, DELETE))) {

		struct connection_info_struct *con_info =
				(struct connection_info_struct *) (*con_cls);
		assert(con_info != NULL);
		if (*upload_data_size != 0) {
			strcpy(&con_info->message[con_info->length], upload_data);
			con_info->length += *upload_data_size;
			*upload_data_size = 0;
			return MHD_YES;

		} else if (NULL != con_info->message) {
			con_info->message[con_info->length] = '\0';
			if (0 == strcmp(method, PUT))
				return doOperation(connection, con_cls, PUT, url);
			else if (0 == strcmp(method, POST))
				return doOperation(connection, con_cls, POST, url);
			else
				return doOperation(connection, con_cls, DELETE, url);
		}
	} else {
		// Methods not implemented
		struct connection_info_struct *con_info =
				(struct connection_info_struct *) (*con_cls);
		assert(con_info != NULL);
		if (*upload_data_size != 0) {
			strcpy(&con_info->message[con_info->length], upload_data);
			con_info->length += *upload_data_size;
			*upload_data_size = 0;
			return MHD_YES;
		} else {
			con_info->message[con_info->length] = '\0';
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
					"++++Method \"%s\" not implemented", method);

			return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
		}
	}

	//Just to remove a warning in the compiler
	return MHD_YES;
}

int RestServer::print_out_key(void *cls, enum MHD_ValueKind kind,
		const char *key, const char *value) {
	logger(ORCH_DEBUG, MODULE_NAME, __FILE__, __LINE__, "%s: %s", key, value);
	return MHD_YES;
}

int RestServer::login(struct MHD_Connection *connection, void **con_cls) {

	int ret = 0, rc = 0;
	struct MHD_Response *response;
	char *username, *password;
	unsigned char hash_token[HASH_SIZE], temp[BUFFER_SIZE];
	char hash_pwd[BUFFER_SIZE], nonce[BUFFER_SIZE], timestamp[BUFFER_SIZE], tmp[BUFFER_SIZE], user_tmp[BUFFER_SIZE];

	struct connection_info_struct *con_info = (struct connection_info_struct *) (*con_cls);
	assert(con_info != NULL);

	if (dbmanager == NULL) {
		con_info->message[con_info->length] = '\0';
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Login can be performed only if authentication is required.");
		return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
	}

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "User login");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Content:");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "%s", con_info->message);

	if (MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Host") == NULL) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "\"Host\" header not present in the request");
		return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
	}

	if (!parsePostBody(*con_info, &username, &password)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Login error: Malformed content");
		return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
	}

	try {

		if (username == NULL || password == NULL) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Client unathorized!");
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

		SHA256((const unsigned char*) password, sizeof(password) - 1, hash_token);

		strcpy(tmp, "");
		strcpy(hash_pwd, "");

		for (int i = 0; i < HASH_SIZE; i++) {
			sprintf(tmp, "%x", hash_token[i]);
			strcat(hash_pwd, tmp);
		}

		strcpy(user_tmp, username);

		if(!dbmanager->userExists(user_tmp, hash_pwd)) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Login failed: wrong username or password!");
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

		if(dbmanager->isLogged(user_tmp)) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Client already logged!");
			return httpResponse(connection, MHD_HTTP_FORBIDDEN);
		}

		rc = RAND_bytes(temp, sizeof(temp));
		if (rc != 1) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "An error occurred while generating nonce!");
			return httpResponse(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
		}

		strcpy(tmp, "");
		strcpy(hash_pwd, "");

		for (int i = 0; i < HASH_SIZE; i++) {
			sprintf(tmp, "%x", temp[i]);
			strcat(nonce, tmp);
		}

		time_t now = time(0);
		tm *ltm = localtime(&now);

		strcpy(timestamp, "");
		sprintf(timestamp, "%d/%d/%d %d:%d", ltm->tm_mday, 1 + ltm->tm_mon, 1900 + ltm->tm_year, ltm->tm_hour, 1 + ltm->tm_min);

		// Insert login information into the database
		dbmanager->insertLogin(user_tmp, nonce, timestamp);

		response = MHD_create_response_from_buffer (strlen((char *)nonce),(void*) nonce, MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header (response, "Content-Type",TOKEN_TYPE);
		MHD_add_response_header (response, "Cache-Control",NO_CACHE);
		ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
		MHD_destroy_response (response);

		return ret;

	} catch (...) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "An error occurred during user login!");
		return httpResponse(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
	}
}

int RestServer::createUser(char *username, struct MHD_Connection *connection, connection_info_struct *con_info) {
	char *group, *password;
	unsigned char hash_token[HASH_SIZE], temp[BUFFER_SIZE];
	char hash_pwd[BUFFER_SIZE], nonce[BUFFER_SIZE], tmp[BUFFER_SIZE], user_tmp[BUFFER_SIZE];

	assert(con_info != NULL);

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "User login");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Content:");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "%s", con_info->message);

	if (MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Host") == NULL) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "\"Host\" header not present in the request");
		return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
	}

	if (!parsePostBody(*con_info, &username, &password, &group)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Create user error: Malformed content");
		return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
	}

	try {
		if (username == NULL || group == NULL || password == NULL) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Client unathorized!");
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

		SHA256((const unsigned char*) password, sizeof(password) - 1, hash_token);

		strcpy(tmp, "");
		strcpy(hash_pwd, "");

		for (int i = 0; i < HASH_SIZE; i++) {
			sprintf(tmp, "%x", hash_token[i]);
			strcat(hash_pwd, tmp);
		}

		strcpy(user_tmp, username);

		if(dbmanager->userExists(user_tmp, hash_pwd)) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Create user failed: wrong username or password!");
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

		strcpy(tmp, "");
		strcpy(hash_pwd, "");

		for (int i = 0; i < HASH_SIZE; i++) {
			sprintf(tmp, "%x", temp[i]);
			strcat(nonce, tmp);
		}

		char *token = (char *) MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "X-Auth-Token");

		if(token == NULL) {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "\"Token\" header not present in the request");
			return false;
		}

		dbmanager->insertUser(user_tmp, nonce, group);

		user_info_t *u = dbmanager->getUserByToken(token);

		std::cout<<"user by token: "<<u->user<<std::endl;
		dbmanager->insertResource(BASE_URL_USER, user_tmp, u->user);

		return httpResponse(connection, MHD_HTTP_ACCEPTED);

	} catch (...) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "An error occurred during user login!");
		return httpResponse(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
	}
}


bool RestServer::parsePostBody(struct connection_info_struct &con_info,
		char **user, char **pwd) {
	Value value;
	read(con_info.message, value);
	return parseLoginForm(value, user, pwd);
}

bool RestServer::parsePostBody(struct connection_info_struct &con_info,
		char **user, char **pwd, char **group) {
	Value value;
	read(con_info.message, value);
	return parseUserCreationForm(value, pwd, group);
}

bool RestServer::parseLoginForm(Value value, char **user, char **pwd) {
	try {
		Object obj = value.getObject();

		bool foundUser = false, foundPwd = false;

		//Identify the flow rules
		for (Object::const_iterator i = obj.begin(); i != obj.end(); ++i) {
			const string& name = i->first;
			const Value& value = i->second;

			if (name == USER) {
				foundUser = true;
				(*user) = (char *) value.getString().c_str();
			} else if (name == PASS) {
				foundPwd = true;
				(*pwd) = (char *) value.getString().c_str();
			} else {
				logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
						"Invalid key: %s", name.c_str());
				return false;
			}
		}
		if (!foundUser) {
			logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
					"Key \"%s\" not found", USER);
			return false;
		} else if (!foundPwd) {
			logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
					"Key \"%s\" not found", PASS);
			return false;
		}
	} catch (exception& e) {
		logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
				"The content does not respect the JSON syntax: ", e.what());
		return false;
	}

	return true;
}

bool RestServer::parseUserCreationForm(Value value, char **pwd, char **group) {
	try {
		Object obj = value.getObject();

		bool foundPwd = false, foundGroup = false;

		//Identify the flow rules
		for (Object::const_iterator i = obj.begin(); i != obj.end(); ++i) {
			const string& name = i->first;
			const Value& value = i->second;

			if (name == PASS) {
				foundPwd = true;
				(*pwd) = (char *) value.getString().c_str();
			} else if (name == GROUP) {
				foundGroup = true;
				(*group) = (char *) value.getString().c_str();
			} else {
				logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
						"Invalid key: %s", name.c_str());
				return false;
			}
		}
		if (!foundPwd) {
			logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
					"Key \"%s\" not found", PASS);
			return false;
		} else if (!foundGroup) {
			logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
					"Key \"%s\" not found", GROUP);
		}
	} catch (exception& e) {
		logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
				"The content does not respect the JSON syntax: ", e.what());
		return false;
	}

	return true;
}


int RestServer::createGraphFromFile(string toBeCreated) {
	char graphID[BUFFER_SIZE];
	strcpy(graphID, GRAPH_ID);

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Graph ID: %s", graphID);
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Graph content:");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "%s",
			toBeCreated.c_str());

	string gID(graphID);
	highlevel::Graph *graph = new highlevel::Graph(gID);

	if (!parseGraphFromFile(toBeCreated, *graph, true)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Malformed content");
		return 0;
	}

	graph->print();
	try {
		if (!gm->newGraph(graph)) {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
					"The graph description is not valid!");
			return 0;
		}
	} catch (...) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__,
				"An error occurred during the creation of the graph!");
		return 0;
	}

	return 1;
}

bool RestServer::parseGraphFromFile(string toBeCreated, highlevel::Graph &graph,
		bool newGraph) //startup. cambiare nome alla funzione
		{
	Value value;
	read(toBeCreated, value);
	return GraphParser::parseGraph(value, graph, newGraph, gm);
}

bool RestServer::parsePutBody(struct connection_info_struct &con_info,
		highlevel::Graph &graph, bool newGraph) {
	Value value;
	read(con_info.message, value);
	return GraphParser::parseGraph(value, graph, newGraph, gm);
}


int RestServer::doOperationOnResource(struct MHD_Connection *connection, struct connection_info_struct *con_info, user_info_t *usr, const char *method, const char *generic_resource) {

	// If security is required, check that the generic resource exist
	if (dbmanager != NULL && !dbmanager->resourceExists(generic_resource)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Resource \"%s\" does not exist", generic_resource);
		return httpResponse(connection, MHD_HTTP_NOT_FOUND);
	}

	if(strcmp(method, GET) == 0) {
		// If security is required, check READ permission on the generic resource
		if (dbmanager != NULL && !secmanager->isAuthorized(usr, _READ, generic_resource)) {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "User not authorized to execute %s \"%s\"", method, generic_resource);
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

		// Case currently implemented: read a graph
		if (strcmp(generic_resource, BASE_URL_GRAPH) == 0)
			return readMultipleGraphs(connection, usr);

		return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
	} else if(strcmp(method, PUT) == 0) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "There are no operations using PUT with generic resources");
		return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
	} else if(strcmp(method, DELETE) == 0) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "There are no operations using DELETE with generic resources");
		return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
	} else if(strcmp(method, POST) == 0) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "There are no operations using POST with generic resources");
		return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
	}

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Method %s is currently not supported to operate on generic resources", method);
	return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
}

/*
 * Version for single resources
 */
int RestServer::doOperationOnResource(struct MHD_Connection *connection, struct connection_info_struct *con_info, user_info_t *usr, const char *method, const char *generic_resource, const char *resource) {

	// GET: can be only read... at the moment!
	if(strcmp(method, GET) == 0) {

		// If security is required, check READ authorization
		if (dbmanager != NULL && !secmanager->isAuthorized(usr, _READ, generic_resource, resource)) {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "User not authorized to execute %s on %s", method, resource);
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

		// Case currently implemented: read a graph
		if ( strcmp(generic_resource, BASE_URL_GRAPH) == 0)
			return readGraph(connection, (char *) resource);

	// PUT: for single resource, it can be only creation... at the moment!
	} else if(strcmp(method, PUT) == 0) {

		// If security is required, check CREATE authorization
		if (dbmanager != NULL && !secmanager->isAuthorized(usr, _CREATE, generic_resource, resource)) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "User not authorized to execute %s on %s", method, resource);
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

		if(strcmp(generic_resource, BASE_URL_GRAPH) == 0)
			return deployNewGraph(connection, con_info, (char *) resource, usr->user);

	// DELETE: for single resource, it can be only deletion... at the moment!
	} else if(strcmp(method, DELETE) == 0) {

		// Check authorization for deleting the single resource
		if (dbmanager != NULL && !secmanager->isAuthorized(usr, _DELETE, generic_resource, resource)) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "User not authorized to execute %s on %s", method, resource);
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

		if(strcmp(generic_resource, BASE_URL_GRAPH) == 0)
			return deleteGraph(connection, (char *) resource);
	} else if(strcmp(method, POST) == 0) {

		if(strcmp(generic_resource, BASE_URL_USER) == 0) {
			// Check authorization
			if (dbmanager != NULL && !secmanager->isAuthorized(usr, _CREATE, generic_resource, resource)) {
				logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "User not authorized to execute %s on %s", method, resource);
				return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
			}

			return createUser((char *) resource, connection, con_info);
		}

		return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
	}

	logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Error: %s on /%s/%s not implemented!", method, generic_resource, resource);
	return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
}

int RestServer::doOperationOnResource(struct MHD_Connection *connection, struct connection_info_struct *con_info, user_info_t *usr, const char *method, const char *generic_resource, const char *resource, const char *extra_info) {

	// GET: can be only read... at the moment!
	if(strcmp(method, GET) == 0) {

		// If security is required, check READ authorization
		if (dbmanager != NULL && !secmanager->isAuthorized(usr, _READ, generic_resource, resource)) {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "User not authorized to execute %s on %s", method, resource);
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

	// PUT: for single resource + extra, it can be only adding extra detail (UPDATE) (e.g. a new flow on a graph)
	} else if(strcmp(method, PUT) == 0) {

		// If security is required, check UPDATE authorization
		if (dbmanager != NULL && !secmanager->isAuthorized(usr, _UPDATE, generic_resource, resource)) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "User not authorized to execute %s on %s", method, resource);
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

		if(strcmp(generic_resource, BASE_URL_GRAPH) == 0)
			return addNewFlow(connection, con_info, (char *) resource, (char *) extra_info);

	// DELETE: for single resource + extra, it can be only UPDATE, e.g. removing a flow in a graph
	} else if(strcmp(method, DELETE) == 0) {

		// If security is required, check UPDATE authorization
		if (dbmanager != NULL && !secmanager->isAuthorized(usr, _UPDATE, generic_resource, resource)) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "User not authorized to execute %s on %s", method, resource);
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}

		if(strcmp(generic_resource, BASE_URL_GRAPH) == 0)
			return deleteFlow(connection, (char *) resource, (char *) extra_info);
	}

	// POST: currently not supported
	else if(strcmp(method, POST) == 0) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Error: POST on /%s/%s/%s not implemented!", generic_resource, resource, extra_info);
		return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
	}

	logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Error: %s on /%s/%s/%s not implemented!", method, generic_resource, resource, extra_info);
	return httpResponse(connection, MHD_HTTP_NOT_IMPLEMENTED);
}


int RestServer::httpResponse(struct MHD_Connection *connection, int code) {
	struct MHD_Response *response;

	response = MHD_create_response_from_buffer(0, (void*) "", MHD_RESPMEM_PERSISTENT);
	int ret = MHD_queue_response(connection, code, response);
	MHD_destroy_response(response);
	return ret;
}

int RestServer::doOperation(struct MHD_Connection *connection, void **con_cls, const char *method, const char *url) {
	char delimiter[] = "/";
	char *generic_resource = NULL, *resource = NULL, *extra = NULL;
	int ret = 0;

	user_info_t *usr = NULL;

	struct connection_info_struct *con_info = (struct connection_info_struct *) (*con_cls);
	assert(con_info != NULL);

	// The stuff below is a sequence of routing checks for HTTP requests (both header and body)... ***********************

	// Check Host field in HTTP header
	if (MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Host") == NULL) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "\"Host\" header not present in the request");
		return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
	}

	// HTTP body must be empty in GET and DELETE requests
	if(strcmp(method, GET) == 0 || strcmp(method, DELETE) == 0) {
		if (con_info->length != 0) {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "%s with body is not allowed", method);
			return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
		}

	// PUT and POST requests must contain JSON data in their body
	} else if(strcmp(method, PUT) == 0 || strcmp(method, POST) == 0) {
		const char *c_type = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Content-Type");
		if (strcmp(c_type, JSON_C_TYPE) != 0) {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Content-Type must be: "JSON_C_TYPE);
			return httpResponse(connection, MHD_HTTP_UNSUPPORTED_MEDIA_TYPE);
		}
	}

	// ...end of routine HTTP requests checks **************************************************************************************

	// If security is required, check whether the current message is a login request
	if(dbmanager != NULL && isLoginRequest(method, url)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Received a login request!");
		// execute login routine
		return login(connection, con_cls);
	}

	// If security is required, try to authenticate the client
	char *token = NULL;
	if (dbmanager != NULL) {
		token = (char *) MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "X-Auth-Token");

		if(token == NULL) {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "\"Token\" header not present in the request");
			return false;
		}

		if(!secmanager->isAuthenticated(connection, token)) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "User not authenticated: request rejected!");
			return httpResponse(connection, MHD_HTTP_UNAUTHORIZED);
		}
	}

	// Fetch from URL the generic resource name, resource name and extra info
	generic_resource = strtok((char *) url, delimiter);
	resource = strtok(NULL, delimiter);
	extra = strtok(NULL, delimiter);

#ifdef ENABLE_DIRECT_VM2VM
	if(strcmp(generic_resource, BASE_URL_ATTACH) == 0) {
		if(resource == NULL)
			return attachDevice(connection, con_cls);
		else {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
				"Bad URL request");
			return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
		}
	} else if(strcmp(generic_resource, BASE_URL_DETACH) == 0) {
		if(resource == NULL)
			return detachDevice(connection, con_cls);
		else {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
				"Bad URL request");
			return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
		}
	} else if(strcmp(generic_resource, BASE_URL_SEND_DPDK) == 0) {
		if(resource == NULL)
			return sendToDPDK(connection, con_cls);
		else {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
				"Bad URL request");
			return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
		}
	}
#endif

	// Fetch user information
	if(dbmanager != NULL)
		usr = dbmanager->getUserByToken(token);

	// If operation on a generic resource (e.g. /NF-FG)
	if(generic_resource != NULL && resource == NULL && extra == NULL)
		ret = doOperationOnResource(connection, con_info, usr, method, generic_resource);

	// If operation on a single resource (e.g. /NF-FG/myGraph)
	else if(generic_resource != NULL && resource != NULL && extra == NULL)
		ret = doOperationOnResource(connection, con_info, usr, method, generic_resource, resource);

	// If operation on a specific feature of a single resource (e.g. /NF-FG/myGraph/flowID)
	else
		ret = doOperationOnResource(connection, con_info, usr, method, generic_resource, resource, extra);

	/*
	 * The usr variable points to a memory space that is allocated inside the isAuthenticated() method,
	 * by using malloc(), so I have to free that memory.
	 */
	if(usr != NULL)
		free(usr);

	return ret;
}

int RestServer::readGraph(struct MHD_Connection *connection, char *graphID) {
	struct MHD_Response *response;
	int ret;

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Required resource: %s", graphID);

	// Check whether the graph exists in the local database and in the graph manager
	if (!dbmanager->resourceExists(BASE_URL_GRAPH, graphID) && !gm->graphExists(graphID)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Method GET is not supported for this resource (i.e. it does not exist)");
		response = MHD_create_response_from_buffer(0, (void*) "",
				MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(response, "Allow", PUT);
		ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
				response);
		MHD_destroy_response(response);
		return ret;
	}

	try {
		Object json = gm->toJSON(graphID);
		stringstream ssj;
		write_formatted(json, ssj);
		string sssj = ssj.str();
		char *aux = (char*) malloc(sizeof(char) * (sssj.length() + 1));
		strcpy(aux, sssj.c_str());
		response = MHD_create_response_from_buffer(strlen(aux), (void*) aux,
				MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(response, "Content-Type", JSON_C_TYPE);
		MHD_add_response_header(response, "Cache-Control", NO_CACHE);
		ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
		MHD_destroy_response(response);
		return ret;
	} catch (...) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "An error occurred while retrieving the graph description!");
		return httpResponse(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
	}
}


int RestServer::readMultipleGraphs(struct MHD_Connection *connection, user_info_t *usr) {
	assert(usr != NULL);

	struct MHD_Response *response;
	int ret;

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Required resource: %s", BASE_URL_GRAPH);

	// Check whether NF-FG exists as a generic resourse in the local database
	if (!dbmanager->resourceExists(BASE_URL_GRAPH)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The generic resource %s does not exist in the local database", BASE_URL_GRAPH);
		response = MHD_create_response_from_buffer(0, (void*) "",
				MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(response, "Allow", PUT);
		ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
				response);
		MHD_destroy_response(response);
		return ret;
	}

	try {
		Object nffg;
		Array nffg_array;
		std::list<std::string> names;

		// If security is required, search the names in the database
		if(dbmanager != NULL)
			dbmanager->getAllowedResourcesNames(usr, _READ, BASE_URL_GRAPH, &names);
		else
			// Otherwise, retrieve all the NF-FGs
			gm->getGraphsNames(&names);

		std::list<std::string>::iterator i;

		for(i = names.begin(); i != names.end(); ++i)
			nffg_array.push_back(gm->toJSON(*i));

		nffg[BASE_URL_GRAPH] = nffg_array;

		stringstream ssj;
		write_formatted(nffg, ssj);
		string sssj = ssj.str();
		char *aux = (char*) malloc(sizeof(char) * (sssj.length() + 1));
		strcpy(aux, sssj.c_str());
		response = MHD_create_response_from_buffer(strlen(aux), (void*) aux,
				MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(response, "Content-Type", JSON_C_TYPE);
		MHD_add_response_header(response, "Cache-Control", NO_CACHE);
		ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
		MHD_destroy_response(response);
		return ret;
	} catch (...) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "An error occurred while retrieving the graph description!");
		return httpResponse(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
	}
}

int RestServer::deployNewGraph(struct MHD_Connection *connection, struct connection_info_struct *con_info, char *resource, char *owner) {

	int ret = 0;
	struct MHD_Response *response;

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Received request for deploying %s/%s", BASE_URL_GRAPH, resource);

	// If security is required, check whether the graph already exists in the database
	if(dbmanager != NULL && dbmanager->resourceExists(BASE_URL_GRAPH, resource)) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Error: cannot deploy an already existing graph in the database!");
		return httpResponse(connection, MHD_HTTP_FORBIDDEN);
	}

	// The same check within the graph manager
	if(gm->graphExists(resource)) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Error: cannot deploy an already existing graph in the manager!");
		return httpResponse(connection, MHD_HTTP_FORBIDDEN);
	}

	string gID(resource);

	highlevel::Graph *graph = new highlevel::Graph(gID);

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Resource to be created/updated: %s/%s", BASE_URL_GRAPH, resource);
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Content:");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "%s", con_info->message);

	bool newGraph = true;

	// Check whether the body is well formed
	if (!parsePutBody(*con_info, *graph, newGraph)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Malformed content");
		return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
	}

	graph->print();

	try {

		// Deploy the new graph
		if (!gm->newGraph(graph)) {
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The graph description is not valid!");
			return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
		}

	} catch (...) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "An error occurred during the creation of the graph!");
		return httpResponse(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
	}

	// If security is required, update database
	if(dbmanager != NULL)
		dbmanager->insertResource(BASE_URL_GRAPH, resource, owner);

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The graph has been properly created!");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "");

	//TODO: put the proper content in the answer
	response = MHD_create_response_from_buffer(0, (void*) "", MHD_RESPMEM_PERSISTENT);
	stringstream absolute_url;
	absolute_url << REST_URL << ":" << REST_PORT << "/" << BASE_URL_GRAPH << "/" << resource;
	MHD_add_response_header(response, "Location", absolute_url.str().c_str());
	ret = MHD_queue_response(connection, MHD_HTTP_CREATED, response);

	MHD_destroy_response(response);
	return ret;
}

int RestServer::deleteGraph(struct MHD_Connection *connection, char *resource) {

	int ret = 0;
	struct MHD_Response *response;

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Received request for deleting %s/%s", BASE_URL_GRAPH, resource);

	// If security is required, check whether the graph does exist in the database
	if(dbmanager != NULL && !dbmanager->resourceExists(BASE_URL_GRAPH, resource)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Cannot delete a non-existing graph in the database!");
		response = MHD_create_response_from_buffer (0,(void*) "", MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header (response, "Allow", PUT);
		ret = MHD_queue_response (connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
		MHD_destroy_response (response);
		return ret;
	}

	// Check whether the graph does exist in the graph manager
	if (!gm->graphExists(resource)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Cannot delete a non-existing graph in the manager!");
		response = MHD_create_response_from_buffer (0,(void*) "", MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header (response, "Allow", PUT);
		ret = MHD_queue_response (connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
		MHD_destroy_response (response);
		return ret;
	}

	try {

		// Delete the graph
		if (!gm->deleteGraph(resource)) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "deleteGraph returns false!");
			return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
		}

		// If security is required, update database
		if(dbmanager != NULL)
			dbmanager->deleteResource(BASE_URL_GRAPH, resource);

	} catch (...) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "An error occurred during the destruction of the graph!");
		return httpResponse(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
	}

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The graph has been properly deleted!");

	return httpResponse(connection, MHD_HTTP_NO_CONTENT);
}

int RestServer::addNewFlow(struct MHD_Connection *connection, struct connection_info_struct *con_info, char *resource, char *extra_info)  {

	int ret = 0;
	struct MHD_Response *response;
	bool newGraph = false;

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Received request for adding a new flow (%s) to %s/%s", extra_info, BASE_URL_GRAPH, resource);

	// If security is required, check that the information about this graph are stored in the database
	if(dbmanager != NULL && !dbmanager->resourceExists(BASE_URL_GRAPH, resource)) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Cannot add a new flow to a graph not existing in the database!");
		response = MHD_create_response_from_buffer(0, (void*) "", MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(response, "Allow", PUT);
		ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
		MHD_destroy_response(response);
		return ret;
	}

	// Check whether the graph exists in the graph manager
	if (!gm->graphExists(resource)) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Cannot add a new flow to a graph not existing in the graph manager!");
		response = MHD_create_response_from_buffer(0, (void*) "", MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(response, "Allow", PUT);
		ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
		MHD_destroy_response(response);
		return ret;
	}

	// Check whether the flow already exists
	if(gm->flowExists(resource, extra_info)) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Cannot add a flow that already exist!");
		return httpResponse(connection, MHD_HTTP_FORBIDDEN);
	}

	string graphID(resource);
	highlevel::Graph *graph = new highlevel::Graph(graphID);

	// Check whether the body is well-formed
	if (!parsePutBody(*con_info, *graph, newGraph)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Malformed content");
		return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
	}

	try {

		// Update the graph
		if(!gm->updateGraph(graphID, graph)) {
			delete(graph);
			logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The graph description is not valid!");
			return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
		}

	} catch(...) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "An error occurred during the destruction of the flow!");
		return httpResponse(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
	}

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The graph has been properly updated!");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "");

	//TODO: put the proper content in the answer
	response = MHD_create_response_from_buffer (0,(void*) "", MHD_RESPMEM_PERSISTENT);
	stringstream absolute_url;
	absolute_url << REST_URL << ":" << REST_PORT << "/" << BASE_URL_GRAPH << "/" << resource;
	MHD_add_response_header (response, "Location", absolute_url.str().c_str());
	ret = MHD_queue_response (connection, MHD_HTTP_CREATED, response);

	MHD_destroy_response (response);
	return ret;
}

int RestServer::deleteFlow(struct MHD_Connection *connection, char *resource, char *extra_info)  {

	int ret = 0;
	struct MHD_Response *response;

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Received request for deleting a flow (%s) from %s/%s", extra_info, BASE_URL_GRAPH, resource);

	// If security is required, check whether the graph exists
	if(dbmanager != NULL && !dbmanager->resourceExists(BASE_URL_GRAPH, resource)) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Cannot delete a flow from a graph that does not exist in the database!");
		response = MHD_create_response_from_buffer(0, (void*) "", MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(response, "Allow", PUT);
		ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
		MHD_destroy_response(response);
		return ret;
	}

	// Check whether the graph, including the specified flow, exists in the graph manager
	if (!gm->graphExists(resource)) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Cannot delete a flow from a graph that does not exist in the graph manager!");
		response = MHD_create_response_from_buffer(0, (void*) "", MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(response, "Allow", PUT);
		ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
		MHD_destroy_response(response);
		return ret;
	}

	if (!gm->flowExists(resource, extra_info)) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "Cannot delete a non-existing flow!");
		response = MHD_create_response_from_buffer(0, (void*) "", MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(response, "Allow", PUT);
		ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
		MHD_destroy_response(response);
		return ret;
	}

	try {
		if (!gm->deleteFlow(resource, extra_info)) {
			logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "deleteFlow (from Graph Manager) returned false!");
			return httpResponse(connection, MHD_HTTP_BAD_REQUEST);
		}
	} catch(...) {
		logger(ORCH_ERROR, MODULE_NAME, __FILE__, __LINE__, "An error occurred during the destruction of the flow!");
		return httpResponse(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
	}

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The graph has been properly updated!");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "");

	//TODO: put the proper content in the answer
	response = MHD_create_response_from_buffer (0,(void*) "", MHD_RESPMEM_PERSISTENT);
	stringstream absolute_url;
	absolute_url << REST_URL << ":" << REST_PORT << "/" << BASE_URL_GRAPH << "/" << resource;
	MHD_add_response_header (response, "Location", absolute_url.str().c_str());
	ret = MHD_queue_response (connection, MHD_HTTP_CREATED, response);

	MHD_destroy_response (response);
	return ret;
}

#ifdef ENABLE_DIRECT_VM2VM
int RestServer::attachDevice(struct MHD_Connection *connection, void **con_cls)
{
	struct MHD_Response *response;
	int ret;
	string response_string;

	char pci_address[20];
	struct connection_info_struct *con_info = (struct connection_info_struct *)(*con_cls);
	assert(con_info != NULL);

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Content of the request:");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "%s",con_info->message);

	Value value;
	read(con_info->message, value);

	string port;	/* port of the vm the command is directed to */
	string id; 		/* id of the device to plug */
	string type;	/* ivshmem or pci device? */
	string device;	/* device to plug */

	string command;	/* command to execute */

	stringstream stream;

	size_t p, p1;
	unsigned int bus, device_addr, function;
	string bus_s, device_addr_s, function_s;
	char c;

	int count = 0;
	string string_response;	/* response from qemu */

	try {
		Object obj = value.getObject();

		for (Object::const_iterator i = obj.begin(); i != obj.end(); ++i) {
			const string& name  = i->first;
			const Value& value = i->second;

			if (name == DIRECT_VM2VM_PORT) {
				port = value.getString();
				count++;
			} else if (name == DIRECT_VM2VM_ID) {
				id = value.getString();
				count++;
			} else if (name == DIRECT_VM2VM_TYPE) {
				type = value.getString();
				count++;
			} else if (name == DIRECT_VM2VM_DEVICE) {
				device = value.getString();
				count++;
			}
		}

		if(count < 4) {
			logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
					"Info missing in JSON!");
			goto error;
		}

	} catch(exception& e) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The content does not respect the JSON syntax: ",e.what());
		goto error;
	}

	/* fill command to execute */
	command = "device_add " + type + "," + device + ",id=" + id;

	if(!gm->executeCommandReleatedToPort(port, command, string_response)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The command '%s' is not valid!",command.c_str());
		goto error;
	}

	if(!string_response.empty()) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
			"Error plugging device: %s", string_response.c_str());
		goto error;
	}

	if(!gm->executeCommandReleatedToPort(port, "info pci", string_response)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The command '%s' is not valid!",command.c_str());
		goto error;
	}

	/* find relative position within all devices */
	p = string_response.find("id \"" + id + "\"");
	p1 = string_response.rfind("Bus", p);

	/* set position to read the address of this device */
	stream.str(string_response);
	stream.seekg(p1);

	/* read pci address of this device */
	stream >> bus_s >> bus >> c;
	stream >> device_addr_s >> device_addr >> c;
	stream >> function_s >> function >> c;

	snprintf(pci_address, sizeof(pci_address), PCI_PRI_FMT, 0x0, bus, device_addr, function);

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The command '%s' related to the port '%s' has been executed!",command.c_str(),command.c_str());
	response = MHD_create_response_from_buffer (strlen(pci_address), pci_address, MHD_RESPMEM_MUST_COPY);
	ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
	MHD_destroy_response (response);
	return ret;

error:
	response = MHD_create_response_from_buffer (0, NULL, MHD_RESPMEM_PERSISTENT);
	ret = MHD_queue_response (connection, MHD_HTTP_BAD_REQUEST, response);
	MHD_destroy_response (response);
	return ret;
}

int RestServer::detachDevice(struct MHD_Connection *connection, void **con_cls)
{
	struct MHD_Response *response;
	int ret;
	string response_string;

	struct connection_info_struct *con_info = (struct connection_info_struct *)(*con_cls);
	assert(con_info != NULL);

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Content of the request:");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "%s",con_info->message);

	Value value;
	read(con_info->message, value);

	string port;	/* port of the vm the command is directed to */
	string id; 		/* id of the device to plug */
	string command;	/* command to execute */

	int count = 0;
	string string_response;	/* response from qemu */

	try {
		Object obj = value.getObject();

		for (Object::const_iterator i = obj.begin(); i != obj.end(); ++i) {
			const string& name  = i->first;
			const Value& value = i->second;

			if (name == DIRECT_VM2VM_PORT) {
				port = value.getString();
				count++;
			} else if (name == DIRECT_VM2VM_ID) {
				id = value.getString();
				count++;
			}
		}

		if(count < 2) {
			logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
					"Info missing in JSON!");
			goto error;
		}

	} catch(exception& e) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The content does not respect the JSON syntax: ",e.what());
		goto error;
	}

	/* fill command to execute */
	command = "device_del " + id;

	if(!gm->executeCommandReleatedToPort(port, command, string_response)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The command '%s' is not valid!",command.c_str());
		goto error;
	}

	if(!string_response.empty()) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
			"Error un-plugging device: %s", string_response.c_str());
		goto error;
	}

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "The command '%s' related to the port '%s' has been executed!",command.c_str(),command.c_str());
	response = MHD_create_response_from_buffer (0, NULL, MHD_RESPMEM_MUST_COPY);
	ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
	MHD_destroy_response (response);
	return ret;

error:
	response = MHD_create_response_from_buffer (0, NULL, MHD_RESPMEM_PERSISTENT);
	ret = MHD_queue_response (connection, MHD_HTTP_BAD_REQUEST, response);
	MHD_destroy_response (response);
	return ret;
}

int RestServer::sendToDPDK(struct MHD_Connection *connection, void **con_cls)
{
	struct MHD_Response *response;
	int ret;
	string response_string;

	struct connection_info_struct *con_info = (struct connection_info_struct *)(*con_cls);
	assert(con_info != NULL);

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "Content of the request:");
	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__, "%s",con_info->message);

	Value value;
	read(con_info->message, value);

	string port;	/* port of the vm the command is directed to */
	string command;	/* command to execute */

	int count = 0;
	string string_response;	/* response from qemu */

	try {
		Object obj = value.getObject();

		for (Object::const_iterator i = obj.begin(); i != obj.end(); ++i) {
			const string& name  = i->first;
			const Value& value = i->second;

			if (name == DIRECT_VM2VM_PORT) {
				port = value.getString();
				count++;
			} else if (name == DIRECT_VM2VM_COMMAND) {
				command = value.getString();
				count++;
			}
		}

		if(count < 2) {
			logger(ORCH_DEBUG_INFO, MODULE_NAME, __FILE__, __LINE__,
					"Info missing in JSON!");
			goto error;
		}

	} catch(exception& e) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
			"The content does not respect the JSON syntax: ",e.what());
		goto error;
	}

	if(!gm->sendToDPDK(port, command, string_response)) {
		logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
				"The command '%s' is not valid!",command.c_str());
		goto error;
	}

	if(string_response != "OK")
		goto error;

	logger(ORCH_INFO, MODULE_NAME, __FILE__, __LINE__,
			"The DPDK '%s' command related to the port '%s' has been executed!",
			command.c_str(),command.c_str());

	response = MHD_create_response_from_buffer (0, NULL, MHD_RESPMEM_MUST_COPY);
	ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
	MHD_destroy_response (response);
	return ret;

error:
	response = MHD_create_response_from_buffer (0, NULL, MHD_RESPMEM_PERSISTENT);
	ret = MHD_queue_response (connection, MHD_HTTP_BAD_REQUEST, response);
	MHD_destroy_response (response);
	return ret;
}


#endif
