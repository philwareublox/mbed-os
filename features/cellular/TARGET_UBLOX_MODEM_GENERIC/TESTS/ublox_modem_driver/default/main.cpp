#include "mbed.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include "TARGET_UBLOX_MODEM_GENERIC/ublox_modem_driver/UbloxCellularInterface.h"
#include "ublox_low_level_api.h"
#include "UDPSocket.h"
#include "FEATURE_COMMON_PAL/nanostack-libservice/mbed-client-libservice/common_functions.h"
#include "mbed_trace.h"
#define TRACE_GROUP "TEST"

using namespace utest::v1;

// ----------------------------------------------------------------
// COMPILE-TIME MACROS
// ----------------------------------------------------------------

// The credentials of the test SIM
#ifndef TEST_DEFAULT_PIN
// Note: for the JT M2M SIMs, each SIM is set up with a randomly
// generated default PIN.  This is the PIN for the SIM with CCID
// 8944501104169549834.
# define TEST_DEFAULT_PIN "0779"
#endif
#ifndef TEST_APN
# define TEST_APN         "jtm2m"
#endif
#ifndef TEST_USERNAME
# define TEST_USERNAME    NULL
#endif
#ifndef TEST_PASSWORD
# define TEST_PASSWORD    NULL
#endif

// Alternate PIN to use during pin change testing
#ifndef TEST_ALT_PIN
# define TEST_ALT_PIN    "9876"
#endif

// A PIN that is definitely incorrect
#ifndef TEST_INCORRECT_PIN
# define TEST_INCORRECT_PIN "1530"
#endif

// ----------------------------------------------------------------
// PRIVATE VARIABLES
// ----------------------------------------------------------------

// Lock for debug prints
static Mutex mtx;

// An instance of the cellular interface
static UbloxCellularInterface *pInterface = new UbloxCellularInterface(true);

// Connection flag
static bool connection_has_gone_down = false;

// A place to connect to
static const char *host = "2.pool.ntp.org";
static const int port = 123;

// ----------------------------------------------------------------
// PRIVATE FUNCTIONS
// ----------------------------------------------------------------

// Locks for debug prints
static void lock()
{
    mtx.lock();
}

static void unlock()
{
    mtx.unlock();
}

// Callback in case the connection goes down
static void ppp_connection_down_cb(nsapi_error_t err)
{
    connection_has_gone_down = true;
}

// Do a thing over the connection, asserting if things go wrong
static void do_ntp(UbloxCellularInterface *pInterface)
{
    int ntp_values[12] = { 0 };
    time_t TIME1970 = 2208988800U;
    UDPSocket sock;
    SocketAddress nist;

    ntp_values[0] = '\x1b';

    TEST_ASSERT(sock.open(pInterface) == 0)

    TEST_ASSERT(pInterface->gethostbyname(host, &nist) == 0);
    nist.set_port(port);

    tr_debug("UDP: NIST server %s address: %s on port %d.", host,
             nist.get_ip_address(), nist.get_port());

    sock.set_timeout(5000);

    TEST_ASSERT(sock.sendto(nist, (void*) ntp_values, sizeof(ntp_values)) > 0);

    TEST_ASSERT (sock.recvfrom(&nist, (void*) ntp_values, sizeof(ntp_values)) > 0);
    sock.close();

    tr_debug("UDP: Values returned by NTP server:");
    for (size_t i = 0; i < sizeof(ntp_values) / sizeof(ntp_values[0]); ++i) {
        tr_debug("\t[%02d] 0x%"PRIX32, i, common_read_32_bit((uint8_t*) &(ntp_values[i])));
        if (i == 10) {
            const time_t timestamp = common_read_32_bit((uint8_t*) &(ntp_values[i])) - TIME1970;
            struct tm *local_time = localtime(&timestamp);
            if (local_time) {
                char time_string[25];
                if (strftime(time_string, sizeof(time_string), "%a %b %d %H:%M:%S %Y", local_time) > 0) {
                    tr_debug("NTP timestamp is %s.", time_string);
                }
            }
        }
    }
}

// Use a connection, checking that it is good
static void use_connection(UbloxCellularInterface *pModem)
{
    const char * ipAddress = pModem->get_ip_address();
    const char * netMask = pModem->get_netmask();
    const char * gateway = pModem->get_gateway();

    TEST_ASSERT(pModem->isConnected());

    TEST_ASSERT(ipAddress != NULL);
    tr_debug ("IP address %s.", ipAddress);
    TEST_ASSERT(netMask != NULL);
    tr_debug ("Net mask %s.", netMask);
    TEST_ASSERT(gateway != NULL);
    tr_debug ("Gateway %s.", gateway);

    do_ntp(pModem);
    TEST_ASSERT(!connection_has_gone_down);
}

// Drop a connection and check that it has dropped
static void drop_connection(UbloxCellularInterface *pModem)
{
    TEST_ASSERT(pModem->disconnect() == 0);
    TEST_ASSERT(connection_has_gone_down);
    connection_has_gone_down = false;
    TEST_ASSERT(!pModem->isConnected());
}

// ----------------------------------------------------------------
// TESTS
// ----------------------------------------------------------------

// Connect with credentials included in the connect request
void test_connect_credentials() {

    pInterface->deinit();
    TEST_ASSERT(pInterface->connect(TEST_DEFAULT_PIN, TEST_APN, TEST_USERNAME, TEST_PASSWORD) == 0);
    use_connection(pInterface);
    drop_connection(pInterface);
}

// Test with credentials preset
void test_connect_preset_credentials() {

    pInterface->deinit();
    TEST_ASSERT(pInterface->init(TEST_DEFAULT_PIN) == 0);
    pInterface->set_credentials(TEST_APN, TEST_USERNAME, TEST_PASSWORD);
    TEST_ASSERT(pInterface->connect(TEST_DEFAULT_PIN) == 0);
    use_connection(pInterface);
    drop_connection(pInterface);
}

// Test adding and using a SIM pin, then removing it
// NOTE: this uses a local instance of the cellular interface, just for variety
void test_add_sim_pin() {

    UbloxCellularInterface *pLocalInterface = NULL;

    pLocalInterface = new UbloxCellularInterface(true);
    pLocalInterface->connection_lost_notification_cb(ppp_connection_down_cb);

    // Enable PIN checking (which will use the current PIN)
    // and also flag that the PIN should be changed to TEST_ALT_PIN,
    // then try connecting
    pLocalInterface->add_remove_sim_pin_check(false);
    pLocalInterface->change_sim_pin(TEST_ALT_PIN);
    TEST_ASSERT(pLocalInterface->connect(TEST_DEFAULT_PIN, TEST_APN, TEST_USERNAME, TEST_PASSWORD) == 0);
    use_connection(pLocalInterface);
    drop_connection(pLocalInterface);
    delete pLocalInterface;

    pLocalInterface = new UbloxCellularInterface(true);
    pLocalInterface->connection_lost_notification_cb(ppp_connection_down_cb);

    // Now try connecting with TEST_ALT_PIN, flagging that we want to
    // change the PIN back to what it was before
    pInterface->change_sim_pin(TEST_DEFAULT_PIN);
    TEST_ASSERT(pInterface->connect(TEST_ALT_PIN, TEST_APN, TEST_USERNAME, TEST_PASSWORD) == 0);
    use_connection(pLocalInterface);
    drop_connection(pLocalInterface);
    delete pLocalInterface;

    pLocalInterface = new UbloxCellularInterface(true);
    pLocalInterface->connection_lost_notification_cb(ppp_connection_down_cb);

    // Check that it was changed back, and this time
    // use the other way of entering the PIN
    pInterface->set_SIM_pin(TEST_DEFAULT_PIN);
    TEST_ASSERT(pInterface->connect(NULL, TEST_APN, TEST_USERNAME, TEST_PASSWORD) == 0);
    use_connection(pLocalInterface);
    drop_connection(pLocalInterface);
    delete pLocalInterface;

    pLocalInterface = new UbloxCellularInterface(true);
    pLocalInterface->connection_lost_notification_cb(ppp_connection_down_cb);

    // Remove PIN checking again and check that it no
    // longer matters what the PIN is
    pInterface->add_remove_sim_pin_check(true);
    TEST_ASSERT(pInterface->connect(TEST_INCORRECT_PIN, TEST_APN, TEST_USERNAME, TEST_PASSWORD) == 0);
    use_connection(pLocalInterface);
    drop_connection(pLocalInterface);
    delete pLocalInterface;
}

// ----------------------------------------------------------------
// TEST ENVIRONMENT
// ----------------------------------------------------------------

// Setup the test environment
utest::v1::status_t test_setup(const size_t number_of_cases) {
    // Setup Greentea with a timeout
    GREENTEA_SETUP(360, "default_auto");
    return verbose_test_setup_handler(number_of_cases);
}

// Test cases
Case cases[] = {
    Case("Connect with credentials", test_connect_credentials),
    Case("Connect with preset credentials", test_connect_preset_credentials),
    //Case("Add and remove SIM pin", test_add_sim_pin),
};

Specification specification(test_setup, cases);

// ----------------------------------------------------------------
// MAIN
// ----------------------------------------------------------------

int main() {

    mbed_trace_init();

    mbed_trace_mutex_wait_function_set(lock);
    mbed_trace_mutex_release_function_set(unlock);

    pInterface->connection_lost_notification_cb(ppp_connection_down_cb);
    
    // Run tests
    return !Harness::run(specification);
}

// End Of File
