#include "nostrdb.h"
#include "hex.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_DB_DIR "./testdata/sg_test_db"

static void delete_test_db() {
	unlink(TEST_DB_DIR "/data.mdb");
	unlink(TEST_DB_DIR "/lock.mdb");
}

static void test_socialgraph_basic() {
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;

	ndb_default_config(&config);
	config.flags |= NDB_FLAG_SKIP_NOTE_VERIFY;
	delete_test_db();
	mkdir(TEST_DB_DIR, 0755);

	assert(ndb_init(&ndb, TEST_DB_DIR, &config));

	// Create some test pubkeys
	unsigned char alice_pk[32], bob_pk[32], charlie_pk[32];
	memset(alice_pk, 0xAA, 32);
	memset(bob_pk, 0xBB, 32);
	memset(charlie_pk, 0xCC, 32);

	// Build a contact list where Alice follows Bob and Charlie
	// kind 3 event
	const char *contact_list_json =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000001\","
		" \"pubkey\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		" \"created_at\":1234567890,"
		" \"kind\":3,"
		" \"tags\":["
		"  [\"p\",\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"],"
		"  [\"p\",\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"]"
		" ],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	// Process the event
	assert(ndb_process_event(ndb, contact_list_json, strlen(contact_list_json)));

	// Give it more time to process through the ingester pipeline
	usleep(500000); // 500ms

	// Query the social graph
	assert(ndb_begin_query(ndb, &txn));

	// Check if Alice follows Bob
	int follows = ndb_socialgraph_is_following(&txn, ndb, alice_pk, bob_pk);
	assert(follows == 1);

	// Check if Alice follows Charlie
	follows = ndb_socialgraph_is_following(&txn, ndb, alice_pk, charlie_pk);
	assert(follows == 1);

	// Check if Bob follows Alice (should be false)
	follows = ndb_socialgraph_is_following(&txn, ndb, bob_pk, alice_pk);
	assert(follows == 0);

	// Check follower count
	int count = ndb_socialgraph_follower_count(&txn, ndb, bob_pk);
	assert(count == 1); // Bob has 1 follower (Alice)

	ndb_end_query(&txn);

	ndb_destroy(ndb);
	printf("✓ test_socialgraph_basic passed\n");
}

static void test_socialgraph_follow_distance() {
	struct ndb *ndb;
	struct ndb_config config;
	struct ndb_txn txn;

	ndb_default_config(&config);
	config.flags |= NDB_FLAG_SKIP_NOTE_VERIFY;
	delete_test_db();
	mkdir(TEST_DB_DIR, 0755);

	assert(ndb_init(&ndb, TEST_DB_DIR, &config));

	// Setup: Root (00..) follows Alice (AA..), Alice follows Bob (BB..)
	// Expected distances: Root=0, Alice=1, Bob=2

	unsigned char root_pk[32], alice_pk[32], bob_pk[32];
	memset(root_pk, 0x00, 32);
	memset(alice_pk, 0xAA, 32);
	memset(bob_pk, 0xBB, 32);

	// Root follows Alice
	const char *root_contact_list =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000001\","
		" \"pubkey\":\"0000000000000000000000000000000000000000000000000000000000000000\","
		" \"created_at\":1234567890,"
		" \"kind\":3,"
		" \"tags\":[[\"p\",\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"]],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	// Alice follows Bob
	const char *alice_contact_list =
		"{\"id\":\"0000000000000000000000000000000000000000000000000000000000000002\","
		" \"pubkey\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
		" \"created_at\":1234567891,"
		" \"kind\":3,"
		" \"tags\":[[\"p\",\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"]],"
		" \"content\":\"\","
		" \"sig\":\"0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000\"}";

	assert(ndb_process_event(ndb, root_contact_list, strlen(root_contact_list)));
	assert(ndb_process_event(ndb, alice_contact_list, strlen(alice_contact_list)));

	usleep(200000); // 200ms for processing

	assert(ndb_begin_query(ndb, &txn));

	// Check distances
	uint32_t distance = ndb_socialgraph_get_follow_distance(&txn, ndb, root_pk);
	assert(distance == 0); // Root is distance 0

	distance = ndb_socialgraph_get_follow_distance(&txn, ndb, alice_pk);
	assert(distance == 1); // Alice followed by root

	distance = ndb_socialgraph_get_follow_distance(&txn, ndb, bob_pk);
	assert(distance == 2); // Bob followed by Alice (distance 1)

	ndb_end_query(&txn);

	ndb_destroy(ndb);
	printf("✓ test_socialgraph_follow_distance passed\n");
}

int main() {
	test_socialgraph_basic();
	test_socialgraph_follow_distance();

	printf("\nAll social graph tests passed!\n");
	return 0;
}
