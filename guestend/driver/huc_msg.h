/* type defines */
#define PROGRAM_INJECTION 							1
#define PROGRAM_INJECTION_RESULT 					2
#define PROGRAM_INJECTION_AFFINITY 					3
#define PROGRAM_INJECTION_AFFINITY_RESULT			4
#define SHUTDOWN_REQUEST							15
#define ERROR										16
#define RESET										17
#define PIN_ON_SAME									18
#define HT_REMAPPING								19
/* version defines */
#define DEFAULT_VERSION 							1

/*
 * +----+---------+------+----------------+
 * | 0  | version | type | payload length |
 * +----+---------+------+----------------+
 * | 32 |                                 |
 * +----+             payload             |
 * | 64 |                                 |
 * +----+---------------------------------+
 */
struct huc_msg_header {
	uint8_t version;
	uint8_t type;
	uint16_t payload_len;
};

struct huc_msg_t {
	struct huc_msg_header header;
	void *payload;
};
