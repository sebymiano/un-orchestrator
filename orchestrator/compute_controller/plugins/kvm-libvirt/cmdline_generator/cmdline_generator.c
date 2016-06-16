#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_ivshmem.h>
#include <rte_mempool.h>
#include <rte_ring.h>
#include <rte_log.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

#define DPDKR_TX_FORMAT "%s_tx"
#define DPDKR_RX_FORMAT "%s_rx"

#define XDPD_TX_FORMAT "%s-to-nf"
#define XDPD_RX_FORMAT "%s-to-xdpd"

/* These are the names as seen from within the VM, through DPDK aliases (note rx<->tx) */
#define ALIAS_TX_FORMAT "%s_rx"
#define ALIAS_RX_FORMAT "%s_tx"

#define MEMPOOL_METADATA_NAME "OVSMEMPOOL"

int init_dpdk(void);
int setup_metadata(const char* metadata);
int expose_mempool_cmdline(const char * metadata);
int expose_port_cmdline(const char * port_name, const char * alias, const char * metadata);
int write_to_pipe(const char * name, char * buf, size_t lenght);
int write_to_file(const char * name, char * buf, size_t lenght);

int init_dpdk(void)
{
	char * arg[6] = {};

	arg[0] = strdup("./something");
	arg[1] = strdup("--proc-type=secondary");
	arg[2] = strdup("-c");
	arg[3] = strdup("0x01");
	arg[4] = strdup("--");
	arg[5] = NULL;

	int argv = sizeof(arg)/sizeof(*arg) - 1;

	/*
	 * XXX: DPDK versions before bb7c5ab does not reset the getopt library before
	 * using it, so if such library has been used before calling rte_eal_init it
	 * could fail.
	 * bb7c5ab solves the issue and should be included in dpdk 2.2.0
	 */
	optind = 1;
	return rte_eal_init(argv, (char**)arg);
}

/*
 * Find global packet's mempool matching OVS pattern
 */
#define MAX_MEMPOOLS 100
struct mempool_list {
	const struct rte_mempool *mp[MAX_MEMPOOLS];
	int num_mp;
};
static void mempool_check_func(const struct rte_mempool *mp, void *arg)
{
	/*
	 * WARNING: This function searches for mempools with names using specific patterns.
	 *		  Mempools names are given by the DPDK-based vSwitch and the naming
	 *		  convention might change from release to release.
	 */
	const char ovs_mp_prefix[] = "ovs_mp_";
	const char *mp_exact_match[] = { "pool_direct", "pool_indirect", NULL };
	int i;

	struct mempool_list* mpl = (struct mempool_list*)arg;
	RTE_LOG(ERR, APP, "Considering mempool '%s'\n", mp->name);
	if (strncmp(mp->name, ovs_mp_prefix,
		sizeof(ovs_mp_prefix)/sizeof(*ovs_mp_prefix) - 1) == 0) {
		if (mpl->num_mp < MAX_MEMPOOLS) {
				mpl->mp[mpl->num_mp] = mp;
				mpl->num_mp++;
		}
	}

	for (i = 0; mp_exact_match[i]; i++) {
		if (strncmp(mp->name, mp_exact_match[i],
			strlen(mp_exact_match[i])) == 0) {
			if (mpl->num_mp < MAX_MEMPOOLS) {
				mpl->mp[mpl->num_mp] = mp;
				mpl->num_mp++;
			}
		}
	}
}

int expose_mempool_cmdline(const char* metadata)
{
	struct mempool_list mpl;
	int i;

	RTE_LOG(INFO, APP, "Adding mempools to metadata '%s'\n", metadata);

	/* Walk the mempools and stores the ones that hold OVS mbufs in mp */
	mpl.num_mp = 0;
	rte_mempool_walk(mempool_check_func, (void *)&mpl);
	if (mpl.num_mp >= MAX_MEMPOOLS) {
		RTE_LOG(ERR, APP, "Too many mempools to share to metadata '%s'\n",
				metadata);
		goto error;
	}
	if (0 == mpl.num_mp) {
		RTE_LOG(ERR, APP, "No mempool found to share to metadata '%s'\n",
				metadata);
		goto error;
	}

	for (i = 0; i < mpl.num_mp; i++) {
		if (rte_ivshmem_metadata_add_mempool(mpl.mp[i], metadata) < 0) {
			RTE_LOG(ERR, APP, "Failed adding mempool '%s' to metadata '%s'\n",
				mpl.mp[i]->name, metadata);
			goto error;
		}
	}

	return 0;

error:
	return -1;
}

int expose_port_cmdline(const char * port_name, const char * port_alias, const char * metadata)
{
	char ring_name[RTE_RING_NAMESIZE];

	struct rte_ring * rx;
	struct rte_ring * tx;

	int err;

	if (port_alias && (*port_alias == '\0'))
		port_alias = NULL; // Treat empty port_alias string as no alias

	printf("Adding port '%s' (alias '%s') to metadata '%s'\n",
			port_name, port_alias ? port_alias : "N/A", metadata);

	/* look for the transmission ring (OVS variant, then xDPd variant) */
	snprintf(ring_name, RTE_RING_NAMESIZE, DPDKR_TX_FORMAT, port_name);
	tx = rte_ring_lookup(ring_name);
	if(tx == NULL)
	{
		/* Try xDPd variant */
		snprintf(ring_name, RTE_RING_NAMESIZE, XDPD_TX_FORMAT, port_name);
		tx = rte_ring_lookup(ring_name);
		if(tx == NULL)
		{
			printf("TX ring not found for port '%s'!\n", port_name);
			return -1;
		}
	}

	/* look for the reception ring (OVS variant, then xDPd variant) */
	snprintf(ring_name, RTE_RING_NAMESIZE, DPDKR_RX_FORMAT, port_name);
	rx = rte_ring_lookup(ring_name);
	if(rx == NULL)
	{
		/* Try xDPd variant */
		snprintf(ring_name, RTE_RING_NAMESIZE, XDPD_RX_FORMAT, port_name);
		rx = rte_ring_lookup(ring_name);
		if(rx == NULL)
		{
			printf("RX ring not found for port '%s'!\n", port_name);
			return -1;
		}
	}
	/* tx ring os from the ovs point of view, it means that ovs writes to it and
	 * the guest app reads from the it, the same but in inverse order applies to
	 * the rx one
	 */
	err = rte_ivshmem_metadata_add_pmd_ring(port_name, &tx, 1, &rx, 1, metadata);
	if(err) {
		printf("Error adding pmd_ring to medatada: '%s'!\n", metadata);
	}
	if (port_alias)
	{
#ifdef IVSHMEM_RING_ALIAS
	#error "IVSHMEM_RING_ALIAS is not supported when using pmd_rings"
#else
		printf("WARNING: Ring aliases not supported by this build (check IVSHMEM_RING_ALIAS)\n");
#endif
	}

	return 0;
}
/*
int write_to_pipe(const char * name, char * buf, size_t size)
{
	int fd = open(name, O_WRONLY);

	ssize_t n;
	const char *p = buf;
	while (size > 0)
	{
		n = write(fd, p, size);
		if (n <= 0) break;
		p += n;
		size -= n;
	}

	close(fd);

	return (n <= 0) ? -1 : 0;
}
*/
int write_to_file(const char * name, char * buf, size_t size)
{
	(void) size;

	FILE *f = fopen(name, "w");
	if(f == NULL)
		return -1;

	fprintf(f, "%s\n", buf);
	fclose(f);
	return 0;
}

char metadata_name[255] = "\0";

int setup_metadata(const char* metadata)
{
	if (! *metadata_name) {
		strncpy(metadata_name, metadata, sizeof(metadata_name));

		if (rte_ivshmem_metadata_create(metadata_name) < 0)
		{
			printf("Failed to create IVSHMEM metadata '%s'\n", metadata_name);
			return -1;
		}
		printf("Created IVSHMEM metadata '%s'\n", metadata_name);
	}
	return 0;
}

int main(int argc, char * argv[])
{
	char buf[512];
	int i;

	if(argc < 2)
		return -1;

	if(init_dpdk() < 0)
	{
		printf("Failed to init dpdk\n");
		return -1;
	}

	for (i=1; i<argc; ++i) {
		printf("A:%s\n", argv[i]);

		if(!strcmp("-n", argv[i]))
		{
			if((i+1) >= argc) {
				printf("Missing metadat name argument value\n");
				return -1;
			}
			setup_metadata(argv[i+1]);
			i++;
		}
		else if(!strcmp("-m", argv[i]))
		{
			setup_metadata(MEMPOOL_METADATA_NAME);  // Default if not specified by -n
			if(expose_mempool_cmdline(metadata_name) < 0)
			{
				printf("Failed to get mempool commandline\n");
				return -1;
			}
		}
		else if(!strcmp("-p", argv[i]))
		{
			char command[512];

			sprintf(command, "ovs-appctl netdev-dpdk/get-cmdline %s > %s", argv[i+1], argv[i+1]);

			int err = system(command);
			if(err)
				printf("Failed to IVSHMEM expose port %s\n", argv[i+1]);

			return 0;	/* ... */
			//strcpy(metadata_name, argv[i+1]);
			//if((i+2) >= argc) {
			//	printf("Missing port_name or port_alias argument value\n");
			//	return -1;
			//}
            //
			//setup_metadata(argv[i+1]);   // Default if not specified by -n
			//if(expose_port_cmdline(argv[i+1], argv[i+2], metadata_name) < 0)
			//{
			//	printf("Failed to IVSHMEM expose port %s\n", argv[i+1]);
			//	return -1;
			//}
			i += 2;
		}
		else {
			printf("Unknown option: %s", argv[i]);
			return -1;
		}
	}

	if (rte_ivshmem_metadata_cmdline_generate(buf, sizeof(buf), metadata_name) < 0)
	{
		printf("Failed to create IVSHMEM metadata '%s'\n", metadata_name);
		return -1;
	}

	//if(write_to_pipe(argv[2], buf, sizeof(buf) < 0))
	//	return -1;
	if(write_to_file(metadata_name, buf, sizeof(buf) < 0))
		return -1;

	printf("Ready----\n");

	return 0;
}
