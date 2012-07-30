static char bucket[BUCKET_BUF_SIZE];

static uint32_t ready_ct = 0;
static struct stats global_stat = { 0, 0, 0, 0, 0 };
static tube_t default_tube;

static int drain_mode = 0;
static int64_t started_at;
static uint64_t op_ct[TOTAL_OPS], timeout_ct = 0;

static conn_t *dirty;

static const char *op_names[] = {
    "<unknown>",
    CMD_PUT,
    CMD_PEEKJOB,
    CMD_RESERVE,
    CMD_DELETE,
    CMD_RELEASE,
    CMD_BURY,
    CMD_KICK,
    CMD_STATS,
    CMD_JOBSTATS,
    CMD_PEEK_BURIED,
    CMD_USE,
    CMD_WATCH,
    CMD_IGNORE,
    CMD_LIST_TUBES,
    CMD_LIST_TUBE_USED,
    CMD_LIST_TUBES_WATCHED,
    CMD_STATS_TUBE,
    CMD_PEEK_READY,
    CMD_PEEK_DELAYED,
    CMD_RESERVE_TIMEOUT,
    CMD_TOUCH,
    CMD_QUIT,
    CMD_PAUSE_TUBE,
};

