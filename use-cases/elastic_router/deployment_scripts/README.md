# Deployment Scripts (Elastic Router Demo)
These scripts automate the creation of virtual environment for ER-demo. They prepare an Ubuntu (14.04 LTS, 64bit) virtual machine and deploy all required components in there.

## Prerequisites
A host machine must install following software components to run these scripts.

### Vagrant
Vagrant is free and open-source solution used to create and configure lightweight, reproducible, and portable development environments. Its [official website](https://www.vagrantup.com/downloads.html) offers installation binaries for different host platform. Note that Vagrant needs a virtualizer (VirtualBox is recommended in here) to operate with.  

### Oracle VM VirtualBox
VirtualBox is a free and open-source general-purpose virtualizer for x86 hardware. Download and install instructions are available on [official website](https://www.virtualbox.org/wiki/Downloads).  

## Description of Scripts
There are three scripts intended to perform different task towards the creation of virtual environment for ER-demo in guest virtual machine.

* **Vagrantfile**: It fetches an image of ubuntu/trusty64 from Vagrant registry and triggers the execution deployment scripts in it. This script configures memory and number of cores assiged to guest machine. These parameters can be set through command line arguments as explained later in next section. 
* **provioning/deploy.py**: A Python script that handles deployment of components including cadvisor, ramon, pipelinedb, opentsdb, doubledecker, ovs, mmp, ctrl_app, and aggregator. It clones the github repositoryof un-orchestrator and locally builds docker image of components. This gives developers an option to modify cloned github source code of a component and rebuild a new docker image from the modified source code. For some components which are not supposed to be modified in general, their already built images are pulled from docker registry at *gitlab.testbed.se:5000*. In addition, this script also installs *docker-engine* and *git* packages on the guest virtual machine.       
* **provioning/install-playbook.yml**: A YML script that is fed to [Ansible](https://www.ansible.com/) tool for automated deployment of the Orchestrator and its prerequisites including various Linux/Python packages.  

## Usage
There are certain command line parameters which help customize the virtual environment to be created. A list along with description is provided below.

* **--vm-memory**: configures RAM (in MegaBytes) for the guest virtual machine. The default value is 1024 that accounts for 1GB of RAM.
* **--vm-cores**: configures number of CPU cores to be assigned to the guest virtual machine. The default value is 1.
* **--github-branch**: specifies which branch of [UN-orchestrator repository](https://github.com/netgroup-polito/un-orchestrator.git) should be cloned for building docker images from the source code. The default value is *new_elastic_router*. The repository is cloned in guest virtual machine under the path */opt/unify/un-orchestrator*. 
* **--docker-registry-username**: used to specify username for login at Acreo's docker registry *gitlab.testbed.se:5000/*. A login is required to pull docker images for certain components. This is a mandatory argument. 
* **--docker-registry-password**: used to specify password corresponding to above username for login at Acreo's docker registry *gitlab.testbed.se:5000/*. This is also a mandatory argument.

### Up and Running
The following command is used to start the process of building virtual environment where guest virtual machine is configured to use 1GB RAM and 2 CPU cores.

    $ cd <path/to/deployment_scripts> 
    $ sudo vagrant --docker-registry-username=myusername --docker-registry-password=mypassword --vm-memory=1024 --vm-cores=2 up
please replace *myusername* and *mypassword* with actual username and password (without any quotation marks).

This command makes Vagrant to download Ubuntu (trusty64) image from registry and start the provisioning process. It is the provisioning process that in turn executes deployment scripts *deploy.py* and *install-playbook.yml*. Please note that provisioning is a long process which may take several minutes (approx. 90 minutes as shown in our tests). You may see output messages on console when *deploy.py* is being executed however no messages are displayed during the execution of *install-playbook.yml* script. At the end of provisioning process, a status of the execution is displayed. In case of any errors, provisioning process can be triggered again as explained below.  

### Provisioning
As stated above, provisioning process is triggered when you execute above command. However it can also be rerun at anytime using the following command:

    $ cd <path/to/deployment_scripts>
    $ sudo vagrant --docker-registry-username=myusername --docker-registry-password=mypassword provision
please replace *myusername* and *mypassword* with actual username and password (without any quotation marks).

### SSH Access
Once guest virtual machine is up, an SSH access is available with following command:
 
    sudo vagrant ssh
This will log you in as user *vagrant*. Use *sudo* to get root access.

In addition, Vagrant also supports other operations on guest virtual machine. The detail of which is available in Vagrant help.

## Post Deployment
*deploy.py* script allows to rebuild the docker images from the component source code e.g., in case the source code has been modified after it was cloned. (The UN-orchestrator repository is cloned in guest virtual machine under the path: */opt/unify/un-orchestrator*). This script takes following command line arguments:  

* **-i or --interactive**: This puts the script in an interactive mode.
* **-u or --username**: This has direct correspondence with *--docker-registry-username* argument discussed above.
* **-p or --password**: This has direct correspondence with *--docker-registry-password* argument discussed above.
* **-b or --branch**: This has direct correspondence with *--github-branch* argument discussed above.
* **-c or --components**: comma separated list of components to be deployed. *all* stands for whole list of available components. Other possible values can be see using the help switch (-h).

### Example
Within guest virtual machine, *deploy.py* script can be found under the path: */vagarant/provioning*. It may be run as follow:
  
     $ sudo -i
     $ cd /vagrant/provisioning
     $ python deploy.py -i -u myusername -p mypassword -c all
please replace *myusername* and *mypassword* with actual username and password (without any quotation marks).

It is strongly recommended that you run *deploy.py* script as root user to avoid failures which may arise in some cases.
