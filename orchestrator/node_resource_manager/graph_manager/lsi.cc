#include "lsi.h"

LSI::LSI(string controllerAddress, string controllerPort,
		 map<string,string> physical_ports,  map<string, list<unsigned int> > network_functions,
		 vector<VLink> virtual_links, map<string,nf_t>  nf_types, map<string, map<unsigned int, PortType> > a_nfs_ports_type) :
		controllerAddress(controllerAddress), controllerPort(controllerPort),
		nf_types(nf_types.begin(), nf_types.end()),
		virtual_links(virtual_links.begin(),virtual_links.end())
{
	for(map<string,string>::iterator p = physical_ports.begin(); p != physical_ports.end(); p++)
	{
		this->physical_ports[p->first] = 0;
		ports_type[p->first] = p->second;
	}

	//create a port name for the NF, by appending to the real name the identifier
	//of the port
	for(map<string, list< unsigned int> >::iterator nf = network_functions.begin(); nf != network_functions.end(); nf++)
	{
		string nf_name = nf->first;
		
		addNF(nf_name, nf->second, nf_types[nf_name], a_nfs_ports_type[nf_name]);
	}

}

string LSI::getControllerAddress()
{
	return controllerAddress;
}

string LSI::getControllerPort()
{
	return controllerPort;
}

list<string> LSI::getPhysicalPortsName()
{
	list<string> names;
	
	map<string,unsigned int>::iterator p = physical_ports.begin();
	for(; p != physical_ports.end(); p++)
		names.push_back(p->first);
	
	return names;
}

set<string> LSI::getNetworkFunctionsName()
{
	set<string> names;
	
	for(map<string,map<string, unsigned int> >::iterator nf = network_functions.begin(); nf != network_functions.end(); nf++)
		names.insert(nf->first);
	
	return names;
}

map<string,nf_t> LSI::getNetworkFunctionsType()
{
	return nf_types;
}

list<string> LSI::getNetworkFunctionsPortNames(string nf)
{
	list<string> names;

	map<string, unsigned int> ports = network_functions[nf];

	for(map<string, unsigned int>::iterator p = ports.begin(); p != ports.end(); p++)
		names.push_back(p->first);
		
	return names;
}

PortType LSI::getNetworkFunctionPortType(string nf, string port_name)
{
	map<string, map< string, PortType> >::iterator nf_it = nfs_ports_type.find(nf);
	if (nf_it == nfs_ports_type.end())
		return UNDEFINED_PORT;

	map<string, PortType>& ports = nf_it->second;
	map< string, PortType >::iterator port_it = ports.find(port_name);
	if (port_it == ports.end())
		return UNDEFINED_PORT;

	return port_it->second;
}

map<string, list< struct nf_port_info> > LSI::getNetworkFunctionsPortsInfo()
{
	map<string, list<struct nf_port_info> > res;

	for (map<string,map<string, unsigned int> >::iterator nf_it = network_functions.begin(); nf_it != network_functions.end(); ++nf_it) {
		const string& nf_name = nf_it->first;
		map<string, unsigned int>& ports = nf_it->second;
		list<struct nf_port_info> pi_list;
		for (map<string, unsigned int>::iterator port_it = ports.begin(); port_it != ports.end(); ++port_it) {
			const string& port_name = port_it->first;
			struct nf_port_info pi;
			pi.port_name = port_name;
			pi.port_type = getNetworkFunctionPortType(nf_name, port_name);
			pi_list.push_back(pi);
		}
		res[nf_name] = pi_list;
	}

	return res;
}

list<uint64_t> LSI::getVirtualLinksRemoteLSI()
{
	list<uint64_t> dpids;
	
	vector<VLink>::iterator vl = virtual_links.begin();
	for(; vl != virtual_links.end(); vl++)
		dpids.push_back(vl->remote_dpid);
	
	return dpids;
}

void LSI::setDpid(uint64_t dpid)
{
	this->dpid = dpid;
}

bool LSI::setPhysicalPortID(string port, uint64_t id)
{
	if(physical_ports.count(port) == 0)
	{
		assert(0);
		return false;
	}
	
	physical_ports[port] = id;
	return true;
}

bool LSI::setNfPortsID(string nf, map<string, unsigned int> translation)
{
	if(network_functions.count(nf) == 0)
		return false;
	
	map<string, unsigned int> ports = network_functions[nf];
	
	for(map<string, unsigned int>::iterator t = translation.begin(); t != translation.end(); t++)
	{
		if(ports.count(t->first) == 0)
			return false;
		ports[t->first] = t->second;	
	}

	network_functions[nf] = ports;
	return true;
}

void LSI::setNetworkFunctionsPortsNameOnSwitch(string nf, list<string> names)
{
	//TODO: implement checks!!!!
	networkFunctionsPortsNameOnSwitch[nf] = names;
}

void LSI::setVLinkIDs(unsigned int position, unsigned int localID, unsigned int remoteID)
{
	virtual_links[position].local_id = localID;
	virtual_links[position].remote_id = remoteID;
}

uint64_t LSI::getDpid()
{
	return dpid;
}

map<string,unsigned int> LSI::getPhysicalPorts()
{
	return physical_ports;
}

map<string,string> LSI::getPortsType()
{
	return ports_type;
}

map<string,unsigned int> LSI::getNetworkFunctionsPorts(string nf)
{
	return network_functions[nf];
}

list<string> LSI::getNetworkFunctionsPortsNameOnSwitch(string nf)
{
	assert(networkFunctionsPortsNameOnSwitch.count(nf) != 0);

	return networkFunctionsPortsNameOnSwitch[nf];
}

vector<VLink> LSI::getVirtualLinks()
{
	return virtual_links;
}

VLink LSI::getVirtualLink(uint64_t ID)
{
	vector<VLink>::iterator v = virtual_links.begin();
	for(; v != virtual_links.end(); v++)
	{
		if(v->getID() == ID)
			return *v;
	}
	
	//cannot be here!
	assert(0);
	return *v;
}


map<string, uint64_t> LSI::getNFsVlinks()
{
	return nfs_vlinks;
}	

map<string, uint64_t> LSI::getPortsVlinks()
{
	return ports_vlinks;
}	

map<string, uint64_t> LSI::getEndPointsVlinks()
{
	return endpoints_vlinks;
}	


void LSI::setNFsVLinks(map<string, uint64_t> nfs_vlinks)
{
	for(map<string, uint64_t>::iterator it = nfs_vlinks.begin(); it != nfs_vlinks.end(); it++)
		this->nfs_vlinks.insert(*it);
}

void LSI::setPortsVLinks(map<string, uint64_t> ports_vlinks)
{
	for(map<string, uint64_t>::iterator it = ports_vlinks.begin(); it != ports_vlinks.end(); it++)
		this->ports_vlinks.insert(*it);
}

void LSI::setEndPointsVLinks(map<string, uint64_t> endpoints_vlinks)
{
	for(map<string, uint64_t>::iterator it = endpoints_vlinks.begin(); it != endpoints_vlinks.end(); it++)
		this->endpoints_vlinks.insert(*it);
}

void LSI::addNFvlink(string NF, uint64_t vlinkID)
{
	nfs_vlinks[NF] = vlinkID;
}

void LSI::addPortvlink(string port, uint64_t vlinkID)
{
	ports_vlinks[port] = vlinkID;
}

void LSI::addEndpointvlink(string endpoint, uint64_t vlinkID)
{
	endpoints_vlinks[endpoint] = vlinkID;
}

void LSI::removeNFvlink(string nf_port)
{
	if(nfs_vlinks.count(nf_port) == 0)
	{
		assert(0);
		return;
	}
	
	map<string,uint64_t>::iterator it = nfs_vlinks.find(nf_port);
	nfs_vlinks.erase(it);
}

void LSI::removePortvlink(string port)
{
	if(ports_vlinks.count(port) == 0)
	{
		assert(0);
		return;
	}
	
	map<string,uint64_t>::iterator it = ports_vlinks.find(port);
	ports_vlinks.erase(it);
}

void LSI::removeEndPointvlink(string endpoint)
{
	if(endpoints_vlinks.count(endpoint) == 0)
	{
		assert(0);
		return;
	}
	
	map<string,uint64_t>::iterator it = endpoints_vlinks.find(endpoint);
	endpoints_vlinks.erase(it);
}

bool LSI::addNF(string nf_name, list< unsigned int> ports, nf_t type, const map<unsigned int, PortType>& a_nf_ports_type)
{
	map<string, unsigned int> nf_ports;
	map<string, PortType> nf_ports_type;

	for (list< unsigned int>::iterator port_it = ports.begin(); port_it != ports.end(); ++port_it) {
		stringstream ss;
		ss << nf_name << "_" << (*port_it);

		nf_ports[ss.str()] = 0;

		map<unsigned int, PortType>::const_iterator pt_it = a_nf_ports_type.find(*port_it);
		if (pt_it == a_nf_ports_type.end())
			return false;
		nf_ports_type[ss.str()] = pt_it->second;
	}

	network_functions[nf_name] = nf_ports;
	nfs_ports_type[nf_name] = nf_ports_type;
	
	nf_types[nf_name] = type;
	
	return true;
}

int LSI::addVlink(VLink vlink)
{
	//TODO: protect the next operation with a mutex
	int retVal = virtual_links.size();
	virtual_links.insert(virtual_links.end(),vlink);
		
	return retVal;
}

void LSI::removeVlink(uint64_t ID)
{
	for(vector<VLink>::iterator v = virtual_links.begin(); v != virtual_links.end(); v++)
	{
		if(v->getID() == ID)
		{
			virtual_links.erase(v);
			return;
		}
	}
	
	assert(0);
	return;
}

void LSI::removeNF(string nf)
{
	assert(network_functions.count(nf) == nf_types.count(nf));

	map<string,map<string, unsigned int> >::iterator it =  network_functions.find(nf);
	network_functions.erase(it);

	map<string,nf_t>::iterator jt = nf_types.find(nf);
	nf_types.erase(jt);
	return;
}
