#!/usr/bin/python2
import os
import subprocess
from time import sleep
import argparse
import sys

__author__ = 'umar.toseef'

meta_data = {
'cadvisor' : {'repo_name':'un-orchestrator', 'github_link':'https://github.com/netgroup-polito/un-orchestrator.git',
            'comp_path':'use-cases/elastic_router/cadvisor/', 'image_id':'gitlab.testbed.se:5000/cadvisor:latest'},

'ramon' : {'repo_name':'un-orchestrator', 'github_link':'https://github.com/netgroup-polito/un-orchestrator.git',
            'comp_path':'use-cases/elastic_router/ramon/', 'image_id':'gitlab.testbed.se:5000/ramon:latest'},

'mmp' : {'repo_name':'un-orchestrator', 'github_link':'https://github.com/netgroup-polito/un-orchestrator.git',
            'comp_path':'use-cases/elastic_router/MEASURE-MMP/', 'image_id':'gitlab.testbed.se:5000/mmp:latest'},

'pipelinedb' : {'repo_name':'un-orchestrator', 'github_link':'',
            'comp_path':'', 'image_id':'gitlab.testbed.se:5000/pipelinedb:latest'},

'opentsdb' : {'repo_name':'un-orchestrator', 'github_link':'',
            'comp_path':'', 'image_id':'gitlab.testbed.se:5000/opentsdb:latest'},

'aggregator' : {'repo_name':'un-orchestrator', 'github_link':'',
            'comp_path':'', 'image_id':'gitlab.testbed.se:5000/aggregator:latest'},

'doubledecker' : {'repo_name':'un-orchestrator', 'github_link':'https://github.com/netgroup-polito/un-orchestrator.git',
            'comp_path':'use-cases/elastic_router/DoubleDecker/docker/', 'image_id':'gitlab.testbed.se:5000/doubledecker:latest'},

'ovs' : {'repo_name':'un-orchestrator', 'github_link':'https://github.com/netgroup-polito/un-orchestrator.git',
            'comp_path':'', 'image_id':'gitlab.testbed.se:5000/ovs:latest'},

'ctrl_app' : {'repo_name':'un-orchestrator', 'github_link':'https://github.com/netgroup-polito/un-orchestrator.git',
            'comp_path':'NFs/docker/elastic-router/ctrl_app/', 'image_id':'gitlab.testbed.se:5000/ctrl:latest'}
}


class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'


def print_info(str):
    print(bcolors.OKBLUE + str + bcolors.ENDC)


def print_success(str):
    print(bcolors.OKGREEN + str + bcolors.ENDC)


def print_error(str):
    print(bcolors.FAIL + str + bcolors.ENDC)


def run_command_get_code(command, working_directory=None):
    arg_list = command.split()
    return_code = subprocess.call(arg_list, cwd=working_directory)
    return return_code


def run_command_get_output(command, working_directory=None):
    arg_list = command.split()
    p = subprocess.Popen(arg_list, cwd=working_directory,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT)
    return iter(p.stdout.readline, b'')


def is_installed(package):
    command = ('dpkg -s '+str(package))
    res = run_command_get_output(command)
    for line in res:
        if line.startswith('Status:'):
            if "deinstall" in line:
                return False
            elif "install" in line:
                return True
    return False


def install_package(package):
    r = run_command_get_code('sudo apt-get update')
    if not r == 0:
        return False
    r = run_command_get_code('sudo DEBIAN_FRONTEND=noninteractive apt-get -y install '+str(package))
    if not r == 0:
        run_command_get_code('sudo apt-get install -f')
        r = run_command_get_code('sudo apt-get '+str(package))
        if not r == 0:
            return False
    return True


def handle_git_installation():
    # Check if git installed
    if not is_installed('git'):
        print_info("Going to install package: git...")
        success = install_package('git')
        if not success:
            print_error("Error in installing package: git")
            print_error("Aborting...")
            sys.exit(1)
        else:
            print_success("Installation of package was successful: git")
    else:
        print_info("Package already installed: git")


def handle_docker_prerequisites():
    dist = " ".join(run_command_get_output('sudo lsb_release -i'))
    codename = " ".join(run_command_get_output('sudo lsb_release -c')).lower().split()[1].strip()
    if 'Ubuntu' not in dist or codename not in ['trusty', 'wily', 'xenial']:
        print_error("docker-engine is a Prerequisite for this script.")
        print_error("Follow instructions to install docker-engine: https://docs.docker.com/engine/installation/linux/")
        print_error("Aborting...")
        sys.exit(1)
    run_command_get_code('sudo apt-get update')
    run_command_get_code('sudo apt-get install apt-transport-https ca-certificates')
    run_command_get_code('sudo apt-key adv --keyserver hkp://p80.pool.sks-keyservers.net:80 --recv-keys 58118E89F3A912897C070ADBF76221572C52609D')
    run_command_get_code('sudo touch /etc/apt/sources.list.d/docker.list')
    with open('/etc/apt/sources.list.d/docker.list', 'w') as the_file:
        the_file.write("deb https://apt.dockerproject.org/repo ubuntu-"+codename+" main")
    run_command_get_code('sudo apt-get update')
    output = run_command_get_output('apt-cache policy docker-engine')
    if "Version table" not in " ".join(output):
        print_error("docker-engine is a Prerequisite for this script.")
        print_error("Follow instructions to install docker-engine: https://docs.docker.com/engine/installation/linux/")
        print_error("Aborting...")
        sys.exit(1)

    run_command_get_code('apt-get -yq install apparmor')
    o = "".join(run_command_get_output("uname -r"))
    r = run_command_get_code('sudo apt-get -yq install linux-image-extra-'+o)
    if not r == 0:
        print_error("docker-engine is a Prerequisite for this script.")
        print_error("Follow instructions to install docker-engine: https://docs.docker.com/engine/installation/linux/")
        print_error("Aborting...")
        sys.exit(1)


def handle_docker_installation():
    # Check if docker installed
    if not is_installed('docker-engine'):
        print_info("Going to install package: docker-engine...")

        # Prerequisites
        handle_docker_prerequisites()

        success = install_package('docker-engine')
        if not success:
            print_error("Error in installing package docker-engine: see https://docs.docker.com/engine/installation/linux/ubuntulinux/")
            print_error("Aborting...")
            sys.exit(1)
        else:
            print_success("Installation of package was successful: docker-engine")
    else:
        print_info("Package already installed: docker-engine")

    # Run service
    res = run_command_get_output('service docker status')
    if "process" in " ".join(res):
        print_info("service is running: docker")
    else:
        r = run_command_get_code('service docker start')
        if r == 0:
            print "waiting for service to start..."
            sleep(5)
            res = run_command_get_output('service docker status')
            if "process" in " ".join(res):
                print_success("service is running: docker")
            else:
                print_error("Failed to start service: docker")
        else:
            print_error("Failed to start service: docker")


def deploy_component(comp_details):

    if not comp_details['comp_path']:
        return pull_docker_image(comp_details)
    else:
        # Fetch code from github if necessary
        if not os.path.isdir('/opt/unify/'+comp_details['repo_name']+'__'+str(branch)+'/'+comp_details['comp_path']):
            print_info('Cloning repo ...')
            r = run_command_get_code('git clone --recursive -b '+str(branch)+' --single-branch '+comp_details['github_link']+' '+'/opt/unify/'+comp_details['repo_name']+'__'+str(branch))
            if not r == 0:
                print_error("Failed to fetch repo from github:"+comp_details['github_link'])
                return r
            else:
                print_success('Repo cloned under: '+'/opt/unify/'+comp_details['repo_name']+'__'+str(branch))

        # Build docker image
        print_info('Going to build image -> '+comp_details['image_id']+' ...')
        command = "sudo docker build -t "+comp_details['image_id']+" ."
        r = run_command_get_code(command, '/opt/unify/'+comp_details['repo_name']+'__'+str(branch)+'/'+comp_details['comp_path'])
        if r == 0:
            print_success('Image built successfully -> '+comp_details['image_id'])
        else:
            print_error('Image built failed!!! ->'+comp_details['image_id'])
        return r


def pull_docker_image(comp_details):

    print_info('Going to pull image -> '+comp_details['image_id'])
    if not username or not password:
        print_error('You must provide username and password at command line.')
        return 1
    r = run_command_get_code('docker login -u '+username+' -p '+password+' gitlab.testbed.se:5000')
    if not r == 0:
        print_error('Could not login at gitlab.testbed.se:5000\n')
        return r
    r = run_command_get_code('sudo docker pull '+comp_details['image_id'])
    if r == 0:
        print_success('Successfully pulled image -> '+comp_details['image_id'])
    else:
        print_error('Could not pull image -> '+comp_details['image_id'])
    return r


def interact_with_user(do_interaction, component):
    if do_interaction:
        while True:
            i = raw_input('\nProceed with the deployment of '+component+' ? (YES, skip, abort):').lower()
            if not i:
                return 0
            elif i in ['yes','skip','abort']:
                return 0 if i == 'yes' else (1111 if i == 'skip' else -1)
    else:
        return 0


if __name__ == '__main__':
    if os.getuid() != 0:
        print_error("You need to have root privileges to run this script. Execute 'sudo -i' before trying again.")
        sys.exit(1)

    parser = argparse.ArgumentParser(description="Component installation script")
    parser.add_argument(
        '-i',
        "--interactive",
        help="Enter into interactive mode of deployment.",
        action='store_true')
    parser.add_argument(
        "-c",
        "--components",
        help="List of components to be installed, e.g., all, cadvisor, ramon, pipelinedb, opentsdb, doubledecker, ovs, mmp, ctrl_app, aggregator.",
        )
    parser.add_argument(
        "-u",
        "--username",
        help="username for docker registry @ gitlab.testbed.se:5000 ",
        )
    parser.add_argument(
        "-p",
        "--password",
        help="password for docker registry @ gitlab.testbed.se:5000 ",
        )
    parser.add_argument(
        "-b",
        "--branch",
        help="github branch name to clone. Default value: new_elastic_router",
        default="new_elastic_router"
        )

    args = parser.parse_args()
    if not args.components:
        print "-c argument is mandatory\n"
        parser.print_help()
        sys.exit(1)

    username = args.username
    password = args.password
    branch = args.branch
    interactive = args.interactive
    comp_to_install = [c.strip() for c in args.components.split(",")]

    if 'all' in comp_to_install:
        comp_to_install = meta_data.keys()
    else:
        for comp in comp_to_install:
            if comp not in meta_data.keys():
                print_error('Unknown component: '+comp)
                sys.exit(1)

    try:
        # Ensure installation of git
        handle_git_installation()

        # Ensure installation of docker
        handle_docker_installation()

        # deployment_scripts other components
        success_fail = []
        for comp in comp_to_install:
            i = interact_with_user(interactive, comp)
            if i < 0:
                print_info("Aborting on user request...")
                break
            elif i > 0:
                success_fail.append(i)
                continue
            r = deploy_component(meta_data[comp])
            success_fail.append(r)

        print_info("\nSummary of this run\n========================================")
        for idx in range(len(success_fail)):
            if success_fail[idx] == 0:
                print_success(comp_to_install[idx]+' deployed successfully:\t'+meta_data[comp_to_install[idx]]['image_id'])
            elif success_fail[idx] == 1111:
                print_info(comp_to_install[idx]+' deployment skipped:\t'+meta_data[comp_to_install[idx]]['image_id'])
            else:
                print_error(comp_to_install[idx]+' deployment failed:\t'+meta_data[comp_to_install[idx]]['image_id'])
        print_info("========================================\n")
    except KeyboardInterrupt as e:
        print("\n--KeyboardInterrupt--")

    # ip link set docker0 down # brctl delbr docker0 #rm -r /var/lib/docker/network
