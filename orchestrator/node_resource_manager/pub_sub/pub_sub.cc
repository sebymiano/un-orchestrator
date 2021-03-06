#include "pub_sub.h"

zactor_t *DoubleDeckerClient::client = NULL;
bool DoubleDeckerClient::connected = false;
list<publish_t> DoubleDeckerClient::messages;
pthread_mutex_t DoubleDeckerClient::connected_mutex;
// Store the configuration in the DoubleDeckerClient object
char *DoubleDeckerClient::clientName;
char *DoubleDeckerClient::brokerAddress;
char *DoubleDeckerClient::keyPath;
bool keep_looping = true;

bool DoubleDeckerClient::init(char *_clientName, char *_brokerAddress, char *_keyPath)
{
	logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "Inizializing the '%s'",DD_CLIENT_MODULE_NAME);
	logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "\t DD client name: '%s'",_clientName);
	logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "\t DD broker address: '%s'",_brokerAddress);
	logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "\t DD key to be used (path): '%s'",_keyPath);

	DoubleDeckerClient::clientName = _clientName;
	brokerAddress = _brokerAddress;
	keyPath = _keyPath;

	pthread_mutex_init(&connected_mutex, NULL);
	//Start a new thread that waits for events
	pthread_t thread[1];
	pthread_create(&thread[0],NULL,loop,NULL);
#ifdef __x86_64__
	//the following function is not available on all platforms
	pthread_setname_np(thread[0],"DoubleDeckerClient");
#endif

	return true;
}

void *DoubleDeckerClient::loop(void *param)
{
	logger(ORCH_DEBUG_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "DoubleDeckerClient thread started");
	// create a ddactor
	client = ddactor_new(clientName, brokerAddress, keyPath);

	// create internal socket for easy termination 
	zsock_t *notify = zsock_new(ZMQ_PULL);
	zsock_bind(notify, "inproc://ddterm");

	// add client and notify to poller
	zpoller_t *poller = zpoller_new(client, notify, NULL);

	while(keep_looping)
	{
		// wait for either ddmsg or notification
		zsock_t *which = (zsock_t *)zpoller_wait(poller, -1);

		// if message from ddactor
		if (which == (zsock_t *)client) 
		{
			zmsg_t *msg = zmsg_recv(client);
			if (msg == NULL) 
			{
				logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "DDClient:loop:zmsg_recv() returned NULL, was probably interrupted");
			}
			// TODO could break out all this message handling to separate function
			//retrieve the event
			char *event = zmsg_popstr(msg);
			if(streq("reg",event))
			{
				//When the registration is successful
				pthread_mutex_lock(&connected_mutex);
				connected = true;
				pthread_mutex_unlock(&connected_mutex);
				logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "Succcessfully registered on the Double Decker network!");
				free(event);

				//Let's send all the messages stored in the list
				for(list<publish_t>::iterator m = messages.begin(); m != messages.end(); m++)
					publish(m->topic,m->message);
			}
			else if (streq("discon",event))
			{
				logger(ORCH_WARNING, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "Connection with the Double Decker network has been lost!");
				free(event);
				//TODO: what to do in this case?
			}
			else if (streq("pub",event))
			{
				logger(ORCH_WARNING, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "Received a 'publication' event. This event is ignored");
				free(event);
				//TODO: add here a callback that handle the proper event
			}
			else if (streq("data",event))
			{
				logger(ORCH_WARNING, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "Received a 'data' event. This event is ignored");
				free(event);
			}
			else if (streq("$TERM",event))
			{
				char * error = zmsg_popstr(msg);
				logger(ORCH_ERROR, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "Error while trying to connect to the Double Decker network: '%s'",error);
				free(event);
	//			logger(ORCH_ERROR, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "This situation is not handled by the code. Please reboot the orchestrator and check if the broker is running!");
	//			signal(SIGALRM,sigalarm_handler);
	//			alarm(1);
				keep_looping = false;
				break;
			}
		} // - if(which == client) -
		
		// if inproc notification
		if(which == notify)
		{
			//stop the loop
			keep_looping = false;
			break;
		}
	} // - while (keep_looping) -

	logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__,"Terminating DoubleDecker connection...");
	zpoller_destroy(&poller);
	zactor_destroy(&client);
	zsock_destroy(&notify);
	logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__,"DoubleDecker connection terminated");  

	return NULL;
}

void DoubleDeckerClient::terminate()
{
	keep_looping = false;
	// Connect to the internal notification socket and message the thread to shut down
	logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "Stopping the Double Decker client");
	zsock_t *sig = zsock_new_push("inproc://ddterm");
	zsock_send(sig, "s", "shutdown!");
	zsock_destroy(&sig);
}

void DoubleDeckerClient::publish(topic_t topic, const char *message)
{
	assert(client != NULL);

	pthread_mutex_lock(&connected_mutex);
	if(!connected)
	{
		//The client is not connected yet with the Double Decker network, then
		//add the message to a list
		publish_t publish;
		publish.topic = topic;
		publish.message = message;
		messages.push_back(publish);
		pthread_mutex_unlock(&connected_mutex);
		return;
	}
	pthread_mutex_unlock(&connected_mutex);

	logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "Publishing on topic '%s'",topicToString(topic));
	logger(ORCH_INFO, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "Publishing message '%s'", message);

	int len = strlen(message);
	zsock_send(client,"sssb", "publish", topicToString(topic), message,&len, sizeof(len));
}

char *DoubleDeckerClient::topicToString(topic_t topic)
{
	switch(topic)
	{
		case FROG_DOMAIN_DESCRIPTION:
			return "frog:domain-description";
		default:
			assert(0 && "This is impossible!");
			return "";
	}
}
/*
void DoubleDeckerClient::sigalarm_handler(int sig)
{
	logger(ORCH_ERROR, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "Error while trying to connect to the Double Decker network!");
	logger(ORCH_ERROR, DD_CLIENT_MODULE_NAME, __FILE__, __LINE__, "This situation is not handled by the code. Please reboot the orchestrator and check if the broker is running!");
	alarm(1);
}
*/
