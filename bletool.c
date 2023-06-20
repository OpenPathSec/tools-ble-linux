/**
 * BLE advertise and scan tool
 *  t-kubo @ Zettant Inc.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <bluetooth.h>
#include <hci.h>
#include <hci_lib.h>

#define DEVICE_NAME   "hci0"
#define DEFAULT_ADV_HEADER "1F0201060303AAFE1716AAFE80"
#define MAX_PKT_SIZE 32

static struct hci_filter ofilter;
static volatile int signal_received = 0;
static int ble_min_interval = 32;
static int ble_max_interval = 64;

static void sigint_handler(int sig) {
    signal_received = sig;
}

static void hex_dump(char *pref, unsigned char *buf, int len)
{
    printf("%s", pref);
    for (int i = 0; i < len; i++)
        printf("%2.2X", buf[i]);
    printf("  ");

    for (int i = 0; i < len; i++)
        printf("%c", (buf[i] < 0x20 || buf[i] > 0x7e) ? '.' : buf[i]);
    printf("\n");
}

static int open_device(char *dev_name)
{
    int dev_id = hci_devid(dev_name);
    if (dev_id < 0)
        dev_id = hci_get_route(NULL);

    int dd = hci_open_dev(dev_id);
    if (dd < 0) {
        perror("Could not open device");
        exit(1);
    }
    return dd;
}

void ctrl_command(uint8_t ogf, uint16_t ocf, char *data) {
    unsigned char buf[HCI_MAX_EVENT_SIZE], *ptr = buf, tmp[2];
    struct hci_filter flt;
    int i, len, dd;

    dd = open_device(DEVICE_NAME);
    len = (int)(strlen(data)/2);
    
    for (i=0; i<len; i++) {
        memcpy(tmp, &data[i*2], 2);
        *ptr++ = (uint8_t) strtol((const char *)tmp, NULL, 16);
    }
    
    /* Setup filter */
    hci_filter_clear(&flt);
    hci_filter_set_ptype(HCI_EVENT_PKT, &flt);
    hci_filter_all_events(&flt);
    if (setsockopt(dd, SOL_HCI, HCI_FILTER, &flt, sizeof(flt)) < 0) {
        hci_close_dev(dd);
        perror("HCI filter setup failed");
        exit(EXIT_FAILURE);
    }

    if (hci_send_cmd(dd, ogf, ocf, len, buf) < 0) {
        hci_close_dev(dd);
        perror("Send failed");
        exit(EXIT_FAILURE);
    }
    hci_close_dev(dd);
}

void configure(uint16_t min_interval, uint16_t max_interval)
{
    char data[MAX_PKT_SIZE];
    sprintf(data, "%04X%04X0000000000000000000700", htons(min_interval), htons(max_interval));
    ctrl_command(0x08, 0x0006, data);
}

void advertise_on(bool on)
{
    ctrl_command(0x08, 0x000a, on ? "01" : "00");
}

void set_advertisement_data(char *data)
{
    char alldata[64];
    sprintf(alldata, "%s%s", DEFAULT_ADV_HEADER, data);
    for (int i = strlen(alldata); i < 64; i++) {
        alldata[i] = '0';
    }
    ctrl_command(0x08, 0x0008, alldata);
}

static u_char recvbuf[HCI_MAX_EVENT_SIZE];

int read_advertise(int dd, uint8_t *data, int datalen)
{
    int len;
    evt_le_meta_event *meta;
    le_advertising_info *info;
    unsigned char *ptr;

    while ((len = read(dd, recvbuf, sizeof(recvbuf))) < 0) {
        if (errno == EINTR && signal_received == SIGINT) {
            return 0;
        }

        if (errno == EAGAIN || errno == EINTR)
            continue;
    }

    ptr = recvbuf + (1 + HCI_EVENT_HDR_SIZE);
    len -= (1 + HCI_EVENT_HDR_SIZE);
    meta = (void *) ptr;

    info = (le_advertising_info *) (meta->data + 1);
    memcpy(data, info->data, datalen);
    return len;
}

int print_advertising_devices(int dd) {
    struct sigaction sa;
    unsigned char dat[MAX_PKT_SIZE];

    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_NOCLDSTOP;
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);

    while (1) {
        if (read_advertise(dd, dat, MAX_PKT_SIZE) == 0) break;
        hex_dump("", dat, MAX_PKT_SIZE);
    }
    return 0;
}


void lescan_close(int dd)
{
    uint8_t filter_dup = 0;
    if (dd == -1) {
        dd = open_device(DEVICE_NAME);
    } else {
        setsockopt(dd, SOL_HCI, HCI_FILTER, &ofilter, sizeof(ofilter));
    }
    int err = hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000);
    if (err < 0) {
        perror("Disable scan failed");
        exit(1);
    }
    hci_close_dev(dd);
}

int lescan_setup() {
    int err, dd;
    uint8_t own_type = 0x00;
    uint8_t scan_type = 0x00; // passive
    uint8_t filter_policy = 0x00;
    uint16_t interval = htobs(0x0010);
    uint16_t window = htobs(0x0010);
    uint8_t filter_dup = 0;

    dd = open_device(DEVICE_NAME);

    err = hci_le_set_scan_parameters(dd, scan_type, interval, window,
                                     own_type, filter_policy, 1000);
    if (err < 0) {
        lescan_close(-1);
        perror("Set scan parameters failed");
        exit(1);
    }

    err = hci_le_set_scan_enable(dd, 0x01, filter_dup, 1000);
    if (err < 0) {
        hci_close_dev(dd);
        perror("Enable scan failed");
        exit(1);
    }
    
    struct hci_filter nf;
    socklen_t olen;

    olen = sizeof(ofilter);
    if (getsockopt(dd, SOL_HCI, HCI_FILTER, &ofilter, &olen) < 0) {
        hci_close_dev(dd);
        printf("Could not get socket options\n");
        return -1;
    }

    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META_EVENT, &nf);

    if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
        printf("Could not set socket options\n");
        return -1;
    }

    return dd;
}


static void usage(void)
{
    printf("Usage: bletool <-r | -s> [options...]\n");
    printf("Options:\n"
           "\t-r, --read               Receive mode\n"
           "\t-s, --send=HEX_STRING    Send advertisements\n"
           "\t-h, --help               Display help\n"
           "\n"
           "Send (-s) advertisement options:\n"
           "\t-m, --min_interval=MS    Minimum interval between adverts in ms (default: 32)\n"
           "\t-M, --max_interval=MS    Maximum interval between adverts in ms (default: 64)\n"
          );
}

static struct option main_options[] = {
    { "help",         no_argument,       0, 'h' },
    { "read",	        no_argument,       0, 'r' },
    { "send",         required_argument, 0, 's' },
    { "min_interval", required_argument, 0, 'm' },
    { "max_interval", required_argument, 0, 'M' },
    { 0,              no_argument,       0,  0  }
};

int main(int argc, char **argv) {
    int option_index = 0, mode = 0, opt;
    char *send_data;

    while ((opt = getopt_long(argc, argv, "r+s:m:M:h", main_options, &option_index)) != -1) {
        switch (opt) {
        case 'r':
            mode = 1; // receive mode
            break;

        case 's':
            mode = 2;
            send_data = optarg;
            break;

        case 'm':
            ble_min_interval = atoi(optarg);
            break;

        case 'M':
            ble_max_interval = atoi(optarg);
            break;

        case 'h':
        default:
            mode = 0;
        }
    }
    printf("ble_min_interval: %d\n", ble_min_interval);
    printf("ble_max_interval: %d\n", ble_max_interval);

    if (mode == 0) {
        usage();
        exit(0);
    } else if (mode == 1) {
        int dd = lescan_setup();
        print_advertising_devices(dd);
        lescan_close(dd);
    } else if (mode == 2) {
        configure(ble_min_interval, ble_max_interval);
        set_advertisement_data(send_data);
        advertise_on(true);
        sleep(1);
        advertise_on(false);
    } else {
        printf("ERROR: we shouldn't be here\n");
        exit(1);
    }
}
