/**
 * LGMP - Looking Glass Memory Protocol
 * Copyright © 2020-2026 Geoffrey McRae <geoff@hostfission.com>
 * https://github.com/gnif/LGMP
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "test_support.h"
#include "headers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_QUEUE_ID 0x200U

static bool cleanupClients(PLGMPClientQueue * queues, PLGMPClient * clients,
    unsigned int count)
{
  bool success = true;
  for(unsigned int i = 0; i < count; ++i)
  {
    if (queues[i] &&
        !testExpectStatus("cleanup lgmpClientUnsubscribe",
          lgmpClientUnsubscribe(&queues[i]), LGMP_OK))
      success = false;
    if (clients[i])
      lgmpClientFree(&clients[i]);
  }

  return success;
}

static bool allocatePayload(struct TestFixture * fixture,
    const void * data, uint32_t size, PLGMPMemory * payload)
{
  if (!testExpectStatus("lgmpHostMemAlloc",
        lgmpHostMemAlloc(fixture->host, size, payload), LGMP_OK))
    return false;

  memcpy(lgmpHostMemPtr(*payload), data, size);
  return true;
}

static bool containsClientID(const uint32_t * clientIDs,
    unsigned int count, uint32_t expected)
{
  for(unsigned int i = 0; i < count; ++i)
    if (clientIDs[i] == expected)
      return true;

  return false;
}

static bool runBroadcast(void)
{
  bool               success         = false;
  struct TestFixture fixture         = { 0 };
  PLGMPHostQueue     hostQueue       = NULL;
  PLGMPClient        clients[2]      = { NULL, NULL };
  PLGMPClientQueue   clientQueues[2] = { NULL, NULL };
  uint32_t           clientIDs[2]    = { 0U, 0U };
  PLGMPMemory        payload         = NULL;

  const struct LGMPQueueConfig config =
  {
    .queueID     = TEST_QUEUE_ID,
    .numMessages = 4U,
    .subTimeout  = 5000U
  };

  if (!testFixtureInit(&fixture) ||
      !testExpectStatus("lgmpHostQueueNew",
        lgmpHostQueueNew(fixture.host, config, &hostQueue), LGMP_OK) ||
      !testFixtureStart(&fixture))
    goto cleanup;

  for(unsigned int i = 0; i < 2U; ++i)
    if (!testClientInit(&fixture, &clients[i], &clientIDs[i]) ||
        !testExpectStatus("lgmpClientSubscribe",
          lgmpClientSubscribe(clients[i], TEST_QUEUE_ID, &clientQueues[i]),
          LGMP_OK))
      goto cleanup;

  uint32_t     subscribedIDs[32] = { 0U };
  unsigned int subscribedCount   = 0U;
  if (!TEST_CHECK(lgmpHostQueueHasSubs(hostQueue)) ||
      !TEST_CHECK(lgmpHostQueueNewSubs(hostQueue) == 2U) ||
      !TEST_CHECK(lgmpHostQueueNewSubs(hostQueue) == 0U) ||
      !testExpectStatus("lgmpHostGetClientIDs",
        lgmpHostGetClientIDs(hostQueue, subscribedIDs, &subscribedCount),
        LGMP_OK) ||
      !TEST_CHECK(subscribedCount == 2U) ||
      !TEST_CHECK(containsClientID(subscribedIDs, subscribedCount,
          clientIDs[0])) ||
      !TEST_CHECK(containsClientID(subscribedIDs, subscribedCount,
          clientIDs[1])))
    goto cleanup;

  static const char value[] = "broadcast payload";
  const uint32_t messageData = 0x12345678U;
  if (!allocatePayload(&fixture, value, sizeof(value), &payload) ||
      !testExpectStatus("lgmpHostQueuePost",
        lgmpHostQueuePost(hostQueue, messageData, payload), LGMP_OK) ||
      !TEST_CHECK(lgmpHostQueuePending(hostQueue) == 1U) ||
      !TEST_CHECK(lgmpHostQueuePayloadPending(hostQueue, payload)) ||
      !TEST_CHECK(lgmpHostQueueMessagePending(hostQueue, payload,
          messageData)))
    goto cleanup;

  for(unsigned int i = 0; i < 2U; ++i)
  {
    LGMPMessage message = { 0 };
    if (!testExpectStatus("lgmpClientProcess",
          lgmpClientProcess(clientQueues[i], &message), LGMP_OK) ||
        !TEST_CHECK(message.udata == messageData) ||
        !TEST_CHECK(message.size == testHostAllocationSize(sizeof(value))) ||
        !TEST_CHECK(memcmp(message.mem, value, sizeof(value)) == 0) ||
        !testExpectStatus("lgmpClientMessageDone",
          lgmpClientMessageDone(clientQueues[i]), LGMP_OK))
      goto cleanup;

    if (i == 0U &&
        (!TEST_CHECK(lgmpHostQueuePending(hostQueue) == 1U) ||
         !TEST_CHECK(lgmpHostQueuePayloadPending(hostQueue, payload))))
      goto cleanup;
  }

  if (!TEST_CHECK(lgmpHostQueuePending(hostQueue) == 0U) ||
      !TEST_CHECK(!lgmpHostQueuePayloadPending(hostQueue, payload)) ||
      !TEST_CHECK(!lgmpHostQueueMessagePending(hostQueue, payload,
          messageData)))
    goto cleanup;

  for(unsigned int i = 0; i < 2U; ++i)
  {
    LGMPMessage message = { 0 };
    if (!testExpectStatus("empty lgmpClientProcess",
          lgmpClientProcess(clientQueues[i], &message),
          LGMP_ERR_QUEUE_EMPTY) ||
        !testExpectStatus("lgmpClientUnsubscribe",
          lgmpClientUnsubscribe(&clientQueues[i]), LGMP_OK))
      goto cleanup;
  }

  if (!TEST_CHECK(!lgmpHostQueueHasSubs(hostQueue)))
    goto cleanup;

  success = true;

cleanup:
  if (!cleanupClients(clientQueues, clients, 2U))
    success = false;
  if (payload)
    lgmpHostMemFree(&payload);
  if (!testFixtureDestroy(&fixture))
    success = false;
  return success;
}

static bool runTargeted(void)
{
  bool               success         = false;
  struct TestFixture fixture         = { 0 };
  PLGMPHostQueue     hostQueue       = NULL;
  PLGMPClient        clients[2]      = { NULL, NULL };
  PLGMPClientQueue   clientQueues[2] = { NULL, NULL };
  uint32_t           clientIDs[2]    = { 0U, 0U };
  PLGMPMemory        payload         = NULL;

  const struct LGMPQueueConfig config =
  {
    .queueID     = TEST_QUEUE_ID,
    .numMessages = 2U,
    .subTimeout  = 5000U
  };

  if (!testFixtureInit(&fixture) ||
      !testExpectStatus("lgmpHostQueueNew",
        lgmpHostQueueNew(fixture.host, config, &hostQueue), LGMP_OK) ||
      !testFixtureStart(&fixture))
    goto cleanup;

  for(unsigned int i = 0; i < 2U; ++i)
    if (!testClientInit(&fixture, &clients[i], &clientIDs[i]) ||
        !testExpectStatus("lgmpClientSubscribe",
          lgmpClientSubscribe(clients[i], TEST_QUEUE_ID, &clientQueues[i]),
          LGMP_OK))
      goto cleanup;

  static const char value[] = "targeted payload";
  const uint64_t messageData = UINT64_C(0x123456789abcdef0);
  if (!allocatePayload(&fixture, value, sizeof(value), &payload))
    goto cleanup;

  unsigned int recipients = 99U;
  if (!testExpectStatus("null targeted client IDs",
        lgmpHostQueuePostForClients(hostQueue, messageData, payload, NULL,
          1U, &recipients), LGMP_ERR_INVALID_ARGUMENT) ||
      !TEST_CHECK(recipients == 0U) ||
      !TEST_CHECK(!lgmpHostQueuePayloadPending(hostQueue, payload)))
    goto cleanup;

  if (!testExpectStatus("targeted post",
        lgmpHostQueuePostForClients(hostQueue, messageData, payload,
          &clientIDs[0], 1U, &recipients), LGMP_OK) ||
      !TEST_CHECK(recipients == 1U))
    goto cleanup;

  LGMPMessage message = { 0 };
  if (!testExpectStatus("non-target client process",
        lgmpClientProcess(clientQueues[1], &message),
        LGMP_ERR_QUEUE_EMPTY) ||
      !testExpectStatus("target client process",
        lgmpClientProcess(clientQueues[0], &message), LGMP_OK) ||
      !TEST_CHECK(message.udata == messageData) ||
      !TEST_CHECK(message.size == testHostAllocationSize(sizeof(value))) ||
      !TEST_CHECK(memcmp(message.mem, value, sizeof(value)) == 0) ||
      !testExpectStatus("target client done",
        lgmpClientMessageDone(clientQueues[0]), LGMP_OK))
    goto cleanup;

  const uint32_t missingClientID = UINT32_MAX;
  recipients = 99U;
  if (!testExpectStatus("unmatched targeted post",
        lgmpHostQueuePostForClients(hostQueue, messageData, payload,
          &missingClientID, 1U, &recipients), LGMP_OK) ||
      !TEST_CHECK(recipients == 0U) ||
      !TEST_CHECK(lgmpHostQueuePending(hostQueue) == 0U) ||
      !TEST_CHECK(!lgmpHostQueuePayloadPending(hostQueue, payload)))
    goto cleanup;

  success = true;

cleanup:
  if (!cleanupClients(clientQueues, clients, 2U))
    success = false;
  if (payload)
    lgmpHostMemFree(&payload);
  if (!testFixtureDestroy(&fixture))
    success = false;
  return success;
}

static bool runCapacity(void)
{
  bool               success                     = false;
  struct TestFixture fixture                     = { 0 };
  PLGMPHostQueue     hostQueues[LGMP_MAX_QUEUES] = { NULL };
  PLGMPClient        client                      = NULL;
  PLGMPClientQueue   clientQueue                 = NULL;
  PLGMPMemory        payloads[3]                 = { NULL, NULL, NULL };
  uint32_t           clientID                    = 0U;

  if (!testFixtureInit(&fixture) || !TEST_CHECK(LGMP_MAX_QUEUES == 6))
    goto cleanup;

  const struct LGMPQueueConfig invalidConfig =
  {
    .queueID     = TEST_QUEUE_ID,
    .numMessages = 1U,
    .subTimeout  = 5000U
  };
  PLGMPHostQueue invalidQueue = NULL;
  if (!testExpectStatus("one-entry queue",
        lgmpHostQueueNew(fixture.host, invalidConfig, &invalidQueue),
        LGMP_ERR_INVALID_ARGUMENT) ||
      !TEST_CHECK(invalidQueue == NULL))
    goto cleanup;

  for(unsigned int i = 0; i < LGMP_MAX_QUEUES; ++i)
  {
    const struct LGMPQueueConfig config =
    {
      .queueID     = TEST_QUEUE_ID + i,
      .numMessages = 2U,
      .subTimeout  = 5000U
    };
    if (!testExpectStatus("lgmpHostQueueNew",
          lgmpHostQueueNew(fixture.host, config, &hostQueues[i]), LGMP_OK))
      goto cleanup;
  }

  const struct LGMPQueueConfig extraConfig =
  {
    .queueID     = TEST_QUEUE_ID + LGMP_MAX_QUEUES,
    .numMessages = 2U,
    .subTimeout  = 5000U
  };
  PLGMPHostQueue extraQueue = NULL;
  if (!testExpectStatus("seventh lgmpHostQueueNew",
        lgmpHostQueueNew(fixture.host, extraConfig, &extraQueue),
        LGMP_ERR_NO_QUEUES) ||
      !TEST_CHECK(extraQueue == NULL) ||
      !testFixtureStart(&fixture) ||
      !testClientInit(&fixture, &client, &clientID) ||
      !testExpectStatus("sixth queue subscription",
        lgmpClientSubscribe(client,
          TEST_QUEUE_ID + LGMP_MAX_QUEUES - 1U, &clientQueue), LGMP_OK))
    goto cleanup;

  static const char first [] = "first";
  static const char second[] = "second";
  static const char third [] = "third";
  if (!allocatePayload(&fixture, first, sizeof(first), &payloads[0]) ||
      !allocatePayload(&fixture, second, sizeof(second), &payloads[1]) ||
      !allocatePayload(&fixture, third, sizeof(third), &payloads[2]) ||
      !testExpectStatus("first queue post",
        lgmpHostQueuePost(hostQueues[LGMP_MAX_QUEUES - 1U], 1U,
          payloads[0]), LGMP_OK) ||
      !testExpectStatus("second queue post",
        lgmpHostQueuePost(hostQueues[LGMP_MAX_QUEUES - 1U], 2U,
          payloads[1]), LGMP_OK) ||
      !testExpectStatus("full queue post",
        lgmpHostQueuePost(hostQueues[LGMP_MAX_QUEUES - 1U], 3U,
          payloads[2]), LGMP_ERR_QUEUE_FULL) ||
      !TEST_CHECK(lgmpHostQueuePending(
          hostQueues[LGMP_MAX_QUEUES - 1U]) == 2U) ||
      !testExpectStatus("lgmpClientAdvanceToLast",
        lgmpClientAdvanceToLast(clientQueue), LGMP_OK) ||
      !TEST_CHECK(lgmpHostQueuePending(
          hostQueues[LGMP_MAX_QUEUES - 1U]) == 1U))
    goto cleanup;

  LGMPMessage message = { 0 };
  if (!testExpectStatus("last queued message",
        lgmpClientProcess(clientQueue, &message), LGMP_OK) ||
      !TEST_CHECK(message.udata == 2U) ||
      !TEST_CHECK(message.size == testHostAllocationSize(sizeof(second))) ||
      !TEST_CHECK(memcmp(message.mem, second, sizeof(second)) == 0) ||
      !testExpectStatus("last queued message done",
        lgmpClientMessageDone(clientQueue), LGMP_OK) ||
      !testExpectStatus("post after queue drain",
        lgmpHostQueuePost(hostQueues[LGMP_MAX_QUEUES - 1U], 3U,
          payloads[2]), LGMP_OK) ||
      !testExpectStatus("post-drain message",
        lgmpClientProcess(clientQueue, &message), LGMP_OK) ||
      !TEST_CHECK(message.udata == 3U) ||
      !TEST_CHECK(message.size == testHostAllocationSize(sizeof(third))) ||
      !TEST_CHECK(memcmp(message.mem, third, sizeof(third)) == 0) ||
      !testExpectStatus("post-drain message done",
        lgmpClientMessageDone(clientQueue), LGMP_OK))
    goto cleanup;

  success = true;

cleanup:
  if (clientQueue &&
      !testExpectStatus("cleanup lgmpClientUnsubscribe",
        lgmpClientUnsubscribe(&clientQueue), LGMP_OK))
    success = false;
  if (client)
    lgmpClientFree(&client);
  for(unsigned int i = 0; i < 3U; ++i)
    if (payloads[i])
      lgmpHostMemFree(&payloads[i]);
  if (!testFixtureDestroy(&fixture))
    success = false;
  return success;
}

static bool runClientData(void)
{
  bool               success     = false;
  struct TestFixture fixture     = { 0 };
  PLGMPHostQueue     hostQueue   = NULL;
  PLGMPClient        client      = NULL;
  PLGMPClientQueue   clientQueue = NULL;
  uint32_t           clientID    = 0U;

  const struct LGMPQueueConfig config =
  {
    .queueID     = TEST_QUEUE_ID,
    .numMessages = 2U,
    .subTimeout  = 5000U
  };

  if (!testFixtureInit(&fixture) ||
      !testExpectStatus("lgmpHostQueueNew",
        lgmpHostQueueNew(fixture.host, config, &hostQueue), LGMP_OK) ||
      !testFixtureStart(&fixture) ||
      !testClientInit(&fixture, &client, &clientID) ||
      !testExpectStatus("lgmpClientSubscribe",
        lgmpClientSubscribe(client, TEST_QUEUE_ID, &clientQueue), LGMP_OK))
    goto cleanup;

  uint32_t acknowledgedSerial = UINT32_MAX;
  if (!testExpectStatus("initial lgmpClientGetSerial",
        lgmpClientGetSerial(clientQueue, &acknowledgedSerial), LGMP_OK) ||
      !TEST_CHECK(acknowledgedSerial == 0U))
    goto cleanup;

  uint8_t oversized[LGMP_MSGS_SIZE + 1U] = { 0U };
  if (!testExpectStatus("oversized lgmpClientSendData",
        lgmpClientSendData(clientQueue, oversized, sizeof(oversized), NULL),
        LGMP_ERR_INVALID_SIZE))
    goto cleanup;

  static const char first [] = "client message one";
  static const char second[] = "client message two";
  uint32_t firstSerial  = 0U;
  uint32_t secondSerial = 0U;
  if (!testExpectStatus("lgmpClientSendData",
        lgmpClientSendData(clientQueue, first, sizeof(first), &firstSerial),
        LGMP_OK) ||
      !TEST_CHECK(firstSerial == 1U) ||
      !testExpectStatus("lgmpClientTrySendData",
        lgmpClientTrySendData(clientQueue, second, sizeof(second),
          &secondSerial), LGMP_OK) ||
      !TEST_CHECK(secondSerial == 2U))
    goto cleanup;

  char     received[LGMP_MSGS_SIZE] = { 0 };
  size_t   receivedSize             = 0U;
  uint32_t sourceClientID           = 0U;
  if (!testExpectStatus("lgmpHostReadDataWithSource",
        lgmpHostReadDataWithSource(hostQueue, received, sizeof(received),
          &receivedSize, &sourceClientID), LGMP_OK) ||
      !TEST_CHECK(sourceClientID == clientID) ||
      !TEST_CHECK(receivedSize == sizeof(first)) ||
      !TEST_CHECK(memcmp(received, first, sizeof(first)) == 0) ||
      !testExpectStatus("first lgmpHostAckData",
        lgmpHostAckData(hostQueue), LGMP_OK) ||
      !testExpectStatus("first acknowledged serial",
        lgmpClientGetSerial(clientQueue, &acknowledgedSerial), LGMP_OK) ||
      !TEST_CHECK(acknowledgedSerial == firstSerial))
    goto cleanup;

  memset(received, 0, sizeof(received));
  receivedSize = 0U;
  if (!testExpectStatus("lgmpHostReadData",
        lgmpHostReadData(hostQueue, received, sizeof(received),
          &receivedSize), LGMP_OK) ||
      !TEST_CHECK(receivedSize == sizeof(second)) ||
      !TEST_CHECK(memcmp(received, second, sizeof(second)) == 0) ||
      !testExpectStatus("second lgmpHostAckData",
        lgmpHostAckData(hostQueue), LGMP_OK) ||
      !testExpectStatus("second acknowledged serial",
        lgmpClientGetSerial(clientQueue, &acknowledgedSerial), LGMP_OK) ||
      !TEST_CHECK(acknowledgedSerial == secondSerial) ||
      !testExpectStatus("empty lgmpHostReadData",
        lgmpHostReadData(hostQueue, received, sizeof(received),
          &receivedSize), LGMP_ERR_QUEUE_EMPTY))
    goto cleanup;

  success = true;

cleanup:
  if (clientQueue &&
      !testExpectStatus("cleanup lgmpClientUnsubscribe",
        lgmpClientUnsubscribe(&clientQueue), LGMP_OK))
    success = false;
  if (client)
    lgmpClientFree(&client);
  if (!testFixtureDestroy(&fixture))
    success = false;
  return success;
}

static struct LGMPClientMessage * findClientMessage(
    struct TestFixture * fixture, const uint8_t * payload)
{
  const size_t dataOffset = offsetof(struct LGMPClientMessage, data);
  for(size_t at = dataOffset; at + LGMP_MSGS_SIZE <= TEST_MEMORY_SIZE;
      at += LGMP_ALIGNOF(struct LGMPClientMessage))
    if (memcmp(fixture->memory + at, payload, LGMP_MSGS_SIZE) == 0)
      return (struct LGMPClientMessage *)(fixture->memory + at - dataOffset);
  return NULL;
}

static bool readClaimedSize(struct TestFixture * fixture,
    PLGMPHostQueue hostQueue, PLGMPClientQueue clientQueue,
    uint32_t claimedSize, size_t capacity, unsigned int round)
{
  enum { GUARD = 64 };
  uint8_t payload[LGMP_MSGS_SIZE];
  for(unsigned int i = 0; i < LGMP_MSGS_SIZE; ++i)
    payload[i] = (uint8_t)(round * 31U + i * 7U + 1U);

  uint32_t serial = 0U;
  if (!testExpectStatus("lgmpClientSendData",
        lgmpClientSendData(clientQueue, payload, sizeof(payload), &serial),
        LGMP_OK))
    return false;

  struct LGMPClientMessage * message = findClientMessage(fixture, payload);
  if (!TEST_CHECK(message != NULL))
    return false;
  message->size = claimedSize;

  uint8_t buffer[LGMP_MSGS_SIZE + 2U * GUARD];
  memset(buffer, 0xA5, sizeof(buffer));
  size_t receivedSize = SIZE_MAX;

  const bool deliverable =
    claimedSize <= LGMP_MSGS_SIZE && claimedSize <= capacity;
  const LGMP_STATUS status =
    lgmpHostReadData(hostQueue, buffer, capacity, &receivedSize);

  bool untouched = true;
  for(size_t i = deliverable ? claimedSize : 0U; i < sizeof(buffer); ++i)
    untouched = untouched && buffer[i] == 0xA5;

  if (!TEST_CHECK(untouched))
  {
    fprintf(stderr, "claimed %u capacity %zu wrote past the message\n",
        claimedSize, capacity);
    return false;
  }

  if (deliverable)
  {
    if (!testExpectStatus("deliverable lgmpHostReadData", status, LGMP_OK) ||
        !TEST_CHECK(receivedSize == claimedSize) ||
        !TEST_CHECK(memcmp(buffer, payload, claimedSize) == 0) ||
        !testExpectStatus("lgmpHostAckData",
          lgmpHostAckData(hostQueue), LGMP_OK))
    {
      fprintf(stderr, "claimed %u capacity %zu\n", claimedSize, capacity);
      return false;
    }
  }
  else if (!testExpectStatus("refused lgmpHostReadData", status,
        LGMP_ERR_QUEUE_EMPTY) ||
      !TEST_CHECK(receivedSize == SIZE_MAX))
  {
    fprintf(stderr, "claimed %u capacity %zu\n", claimedSize, capacity);
    return false;
  }

  uint32_t acknowledged = 0U;
  if (!testExpectStatus("lgmpClientGetSerial",
        lgmpClientGetSerial(clientQueue, &acknowledged), LGMP_OK) ||
      !TEST_CHECK(acknowledged == serial))
    return false;

  return true;
}

struct ClientDataRig
{
  struct TestFixture fixture;
  PLGMPHostQueue     hostQueue;
  PLGMPClient        client;
  PLGMPClientQueue   clientQueue;
  uint32_t           clientID;
};

typedef bool (* ClientDataCase)(struct ClientDataRig * rig);

static bool runClientDataRig(ClientDataCase run)
{
  struct ClientDataRig rig = { 0 };
  bool success = false;

  const struct LGMPQueueConfig config =
  {
    .queueID     = TEST_QUEUE_ID,
    .numMessages = 2U,
    .subTimeout  = 5000U
  };

  if (testFixtureInit(&rig.fixture) &&
      testExpectStatus("lgmpHostQueueNew",
        lgmpHostQueueNew(rig.fixture.host, config, &rig.hostQueue), LGMP_OK) &&
      testFixtureStart(&rig.fixture) &&
      testClientInit(&rig.fixture, &rig.client, &rig.clientID) &&
      testExpectStatus("lgmpClientSubscribe",
        lgmpClientSubscribe(rig.client, TEST_QUEUE_ID, &rig.clientQueue),
        LGMP_OK))
    success = run(&rig);

  if (rig.clientQueue &&
      !testExpectStatus("cleanup lgmpClientUnsubscribe",
        lgmpClientUnsubscribe(&rig.clientQueue), LGMP_OK))
    success = false;
  if (rig.client)
    lgmpClientFree(&rig.client);
  if (!testFixtureDestroy(&rig.fixture))
    success = false;
  return success;
}

static bool clientDataBounds(struct ClientDataRig * rig)
{
  static const uint32_t claimedSizes[] =
  {
    0U, 1U, 15U, 16U, 17U, LGMP_MSGS_SIZE - 1U, LGMP_MSGS_SIZE,
    LGMP_MSGS_SIZE + 1U, LGMP_MSGS_SIZE + 16U, 4096U, UINT32_MAX
  };
  static const size_t capacities[] =
  {
    0U, 1U, 16U, LGMP_MSGS_SIZE - 1U, LGMP_MSGS_SIZE, LGMP_MSGS_SIZE + 64U
  };

  unsigned int round = 0U;
  for(size_t s = 0; s < sizeof(claimedSizes) / sizeof(*claimedSizes); ++s)
    for(size_t c = 0; c < sizeof(capacities) / sizeof(*capacities); ++c)
      if (!readClaimedSize(&rig->fixture, rig->hostQueue, rig->clientQueue,
            claimedSizes[s], capacities[c], round++))
        return false;
  return true;
}

static struct LGMPHeaderQueue * findHeaderQueue(struct TestFixture * fixture)
{
  struct LGMPHeader * header = (struct LGMPHeader *)fixture->memory;
  for(unsigned int i = 0; i < LGMP_MAX_QUEUES; ++i)
    if (header->queues[i].queueID == TEST_QUEUE_ID)
      return &header->queues[i];
  return NULL;
}

static bool clientDataAvailCorrupt(struct ClientDataRig * rig)
{
  struct LGMPHeaderQueue * hq = findHeaderQueue(&rig->fixture);
  if (!TEST_CHECK(hq != NULL) || !testFixtureStop(&rig->fixture))
    return false;

  static const char message[] = "survives the corrupt counter";
  uint32_t serial = 0U;
  if (!testExpectStatus("lgmpClientSendData",
        lgmpClientSendData(rig->clientQueue, message, sizeof(message),
          &serial), LGMP_OK))
    return false;

  const uint32_t readSerial =
    atomic_load_explicit(&hq->cMsgRSerial, memory_order_relaxed);
  uint8_t buffer[LGMP_MSGS_SIZE];
  size_t  size = 0U;

  static const uint32_t corrupt[] =
    { LGMP_MSGS_MAX + 1U, LGMP_MSGS_MAX * 2U, UINT32_MAX };
  for(size_t i = 0; i < sizeof(corrupt) / sizeof(*corrupt); ++i)
  {
    atomic_store_explicit(&hq->cMsgAvail, corrupt[i], memory_order_relaxed);
    if (!testExpectStatus("corrupt cMsgAvail lgmpHostReadData",
          lgmpHostReadData(rig->hostQueue, buffer, sizeof(buffer), &size),
          LGMP_ERR_CORRUPTED) ||
        !TEST_CHECK(atomic_load_explicit(&hq->cMsgAvail,
          memory_order_relaxed) == corrupt[i]) ||
        !TEST_CHECK(atomic_load_explicit(&hq->cMsgRSerial,
          memory_order_relaxed) == readSerial))
    {
      fprintf(stderr, "cMsgAvail %u\n", corrupt[i]);
      return false;
    }
  }

  atomic_store_explicit(&hq->cMsgAvail, LGMP_MSGS_MAX - 1U,
      memory_order_relaxed);
  return testExpectStatus("restored lgmpHostReadData",
        lgmpHostReadData(rig->hostQueue, buffer, sizeof(buffer), &size),
        LGMP_OK) &&
    TEST_CHECK(size == sizeof(message)) &&
    TEST_CHECK(memcmp(buffer, message, sizeof(message)) == 0);
}

static bool processReportsRing(PLGMPHost host, struct LGMPHeaderQueue * hq,
    const char * state)
{
  for(uint32_t avail = 0U; avail <= LGMP_MSGS_MAX; ++avail)
  {
    atomic_store_explicit(&hq->cMsgAvail, avail, memory_order_relaxed);
    if (!testExpectStatus("in range lgmpHostProcess",
          lgmpHostProcess(host), LGMP_OK))
    {
      fprintf(stderr, "%s cMsgAvail %u\n", state, avail);
      return false;
    }
  }

  static const uint32_t corrupt[] =
    { LGMP_MSGS_MAX + 1U, LGMP_MSGS_MAX * 2U, UINT32_MAX };
  for(size_t i = 0; i < sizeof(corrupt) / sizeof(*corrupt); ++i)
  {
    atomic_store_explicit(&hq->cMsgAvail, corrupt[i], memory_order_relaxed);
    if (!testExpectStatus("corrupt cMsgAvail lgmpHostProcess",
          lgmpHostProcess(host), LGMP_ERR_CORRUPTED) ||
        !TEST_CHECK(atomic_load_explicit(&hq->cMsgAvail,
          memory_order_relaxed) == corrupt[i]))
    {
      fprintf(stderr, "%s cMsgAvail %u\n", state, corrupt[i]);
      return false;
    }
  }

  atomic_store_explicit(&hq->cMsgAvail, LGMP_MSGS_MAX, memory_order_relaxed);
  return true;
}

static bool clientRingCorruptProcess(struct ClientDataRig * rig)
{
  struct LGMPHeaderQueue * hq = findHeaderQueue(&rig->fixture);
  if (!TEST_CHECK(hq != NULL) || !testFixtureStop(&rig->fixture) ||
      !TEST_CHECK(lgmpHostQueuePending(rig->hostQueue) == 0U) ||
      !processReportsRing(rig->fixture.host, hq, "idle queue"))
    return false;

  static const char message[] = "pending host message";
  PLGMPMemory payload = NULL;
  if (!allocatePayload(&rig->fixture, message, sizeof(message), &payload))
    return false;

  const bool posted = testExpectStatus("lgmpHostQueuePost",
      lgmpHostQueuePost(rig->hostQueue, 1U, payload), LGMP_OK);
  const bool success = posted &&
    TEST_CHECK(lgmpHostQueuePending(rig->hostQueue) == 1U) &&
    processReportsRing(rig->fixture.host, hq, "pending queue");
  lgmpHostMemFree(&payload);
  return success;
}

struct RefusalFlood
{
  struct LGMPHeaderQueue * hq;
  struct TestAtomicBool    flooding;
  struct TestAtomicBool    stop;
};

static void floodRefusals(void * opaque)
{
  struct RefusalFlood * flood = (struct RefusalFlood *)opaque;
  while (!testAtomicBoolLoad(&flood->stop))
  {
    for(unsigned int i = 0; i < LGMP_MSGS_MAX; ++i)
      *(volatile uint32_t *)&flood->hq->cMsgs[i].size = LGMP_MSGS_SIZE + 1U;
    atomic_store_explicit(&flood->hq->cMsgAvail, 0U, memory_order_relaxed);
    testAtomicBoolStore(&flood->flooding, true);
  }
}

static bool clientDataRefusalCap(struct ClientDataRig * rig)
{
  struct RefusalFlood flood = { .hq = findHeaderQueue(&rig->fixture) };
  if (!TEST_CHECK(flood.hq != NULL))
    return false;
  testAtomicBoolInit(&flood.flooding, false);
  testAtomicBoolInit(&flood.stop, false);

  struct TestThread thread = { 0 };
  if (!testThreadStart(&thread, floodRefusals, &flood))
    return false;
  while (!testAtomicBoolLoad(&flood.flooding))
    testSleepMS(1);

  bool     success = true;
  uint32_t refused = 0U;
  uint8_t  buffer[LGMP_MSGS_SIZE];
  size_t   size = 0U;
  for(unsigned int call = 0; call < 64U && success; ++call)
  {
    const uint32_t before =
      atomic_load_explicit(&flood.hq->cMsgRSerial, memory_order_relaxed);
    const LGMP_STATUS status =
      lgmpHostReadData(rig->hostQueue, buffer, sizeof(buffer), &size);
    const uint32_t consumed =
      atomic_load_explicit(&flood.hq->cMsgRSerial, memory_order_relaxed) -
      before;
    success = testExpectStatus("flooded lgmpHostReadData", status,
        LGMP_ERR_QUEUE_EMPTY) && TEST_CHECK(consumed <= LGMP_MSGS_MAX);
    refused += consumed;
  }
  success = success && TEST_CHECK(refused > 0U);

  testAtomicBoolStore(&flood.stop, true);
  if (!testThreadJoin(&thread))
    success = false;

  atomic_store_explicit(&flood.hq->cMsgAvail, LGMP_MSGS_MAX,
      memory_order_relaxed);
  return success;
}

int main(int argc, char * argv[])
{
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s CASE\n", argv[0]);
    return EXIT_FAILURE;
  }

  bool success = false;
  if (strcmp(argv[1], "broadcast") == 0)
    success = runBroadcast();
  else if (strcmp(argv[1], "targeted") == 0)
    success = runTargeted();
  else if (strcmp(argv[1], "capacity") == 0)
    success = runCapacity();
  else if (strcmp(argv[1], "client-data") == 0)
    success = runClientData();
  else if (strcmp(argv[1], "client-data-bounds") == 0)
    success = runClientDataRig(clientDataBounds);
  else if (strcmp(argv[1], "client-data-avail-corrupt") == 0)
    success = runClientDataRig(clientDataAvailCorrupt);
  else if (strcmp(argv[1], "client-ring-corrupt-process") == 0)
    success = runClientDataRig(clientRingCorruptProcess);
  else if (strcmp(argv[1], "client-data-refusal-cap") == 0)
    success = runClientDataRig(clientDataRefusalCap);
  else
    fprintf(stderr, "unknown queue test case: %s\n", argv[1]);

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
