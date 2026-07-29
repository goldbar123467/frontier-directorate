#include "fd_internal.h"

#include <string.h>

#define FD_REPLAY_SECTION_COUNT UINT32_C(5)
#define FD_REPLAY_RECORD_WIRE_SIZE UINT64_C(228)
#define FD_REPLAY_AUDIT_WIRE_SIZE UINT64_C(328)

static const uint8_t fd_replay_magic[8] = {
    'F', 'D', 'R', 'P', UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(1)
};

typedef struct fd_replay_section_internal {
    const uint8_t *payload;
    uint64_t length;
    bool present;
} fd_replay_section_internal;

static bool fd_replay_result_is_auditable(fd_result result);

static void fd_replay_footer_digest(const uint8_t *bytes,
                                    uint64_t size,
                                    uint8_t out[32])
{
    static const uint8_t domain[] = "FrontierDirectorate/Replay/Footer/v1";
    fd_sha256_ctx_internal sha;
    uint8_t length[4];
    fd_sha256_init_internal(&sha);
    fd_store_u32_le(length, (uint32_t)(sizeof(domain) - 1U));
    fd_sha256_update_internal(&sha, length, UINT64_C(4));
    fd_sha256_update_internal(&sha, domain, (uint64_t)(sizeof(domain) - 1U));
    fd_sha256_update_internal(&sha, bytes, size);
    fd_sha256_final_internal(&sha, out);
}

static void fd_replay_write_section_header(uint8_t *bytes,
                                           uint64_t position,
                                           uint16_t tag,
                                           uint64_t length)
{
    fd_store_u16_le(bytes + position, tag);
    fd_store_u16_le(bytes + position + UINT64_C(2), FD_SECTION_CRITICAL);
    fd_store_u32_le(bytes + position + UINT64_C(4), UINT32_C(0));
    fd_store_u64_le(bytes + position + UINT64_C(8), length);
}

static void fd_replay_finish_section(uint8_t *bytes,
                                     uint64_t payload_position,
                                     uint64_t length)
{
    uint8_t digest[32];
    (void)fd_sha256(bytes + payload_position, length, digest);
    memcpy(bytes + payload_position + length, digest, 32U);
}

static void fd_encode_replay_record(uint8_t *bytes,
                                    const fd_replay_record_internal *record)
{
    uint32_t seat;
    fd_store_u64_le(bytes, record->tick_before);
    fd_store_u64_le(bytes + 8U, record->tick_after);
    fd_store_u64_le(bytes + 16U, record->decision.expected_tick);
    memcpy(bytes + 24U, record->decision.expected_pre_state_sha256, 32U);
    fd_store_u32_le(bytes + 56U, record->decision.seat_count);
    fd_store_u32_le(bytes + 60U, record->decision.reserved);
    for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        uint8_t *seat_bytes = bytes + 64U + (uint64_t)seat * UINT64_C(32);
        uint32_t parameter;
        fd_store_u64_le(seat_bytes, record->decision.seats[seat].actor_id);
        fd_store_u32_le(seat_bytes + 8U,
                        record->decision.seats[seat].action.category);
        fd_store_u32_le(seat_bytes + 12U,
                        record->decision.seats[seat].action.parameter_count);
        for (parameter = 0U; parameter < 4U; ++parameter) {
            fd_store_u32_le(seat_bytes + 16U + (uint64_t)parameter * UINT64_C(4),
                            record->decision.seats[seat].action.parameters[parameter]);
        }
    }
    memcpy(bytes + 128U, record->pre_hash, 32U);
    memcpy(bytes + 160U, record->post_hash, 32U);
    memcpy(bytes + 192U, record->event_hash, 32U);
    fd_store_u32_le(bytes + 224U, record->rng_domain_count);
}

static fd_result fd_decode_replay_record(const uint8_t *bytes,
                                         uint64_t index,
                                         fd_replay_record_internal *record,
                                         fd_diagnostic *diag)
{
    uint32_t seat;
    memset(record, 0, sizeof(*record));
    record->tick_before = fd_load_u64_le(bytes);
    record->tick_after = fd_load_u64_le(bytes + 8U);
    record->decision.struct_size = (uint32_t)sizeof(record->decision);
    record->decision.abi_version = FD_ABI_VERSION;
    record->decision.expected_tick = fd_load_u64_le(bytes + 16U);
    memcpy(record->decision.expected_pre_state_sha256, bytes + 24U, 32U);
    record->decision.seat_count = fd_load_u32_le(bytes + 56U);
    record->decision.reserved = fd_load_u32_le(bytes + 60U);
    for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        const uint8_t *seat_bytes = bytes + 64U + (uint64_t)seat * UINT64_C(32);
        uint32_t parameter;
        record->decision.seats[seat].actor_id = fd_load_u64_le(seat_bytes);
        record->decision.seats[seat].action.struct_size =
            (uint32_t)sizeof(record->decision.seats[seat].action);
        record->decision.seats[seat].action.abi_version = FD_ABI_VERSION;
        record->decision.seats[seat].action.category =
            fd_load_u32_le(seat_bytes + 8U);
        record->decision.seats[seat].action.parameter_count =
            fd_load_u32_le(seat_bytes + 12U);
        for (parameter = 0U; parameter < 4U; ++parameter) {
            record->decision.seats[seat].action.parameters[parameter] =
                fd_load_u32_le(seat_bytes + 16U +
                               (uint64_t)parameter * UINT64_C(4));
        }
    }
    memcpy(record->pre_hash, bytes + 128U, 32U);
    memcpy(record->post_hash, bytes + 160U, 32U);
    memcpy(record->event_hash, bytes + 192U, 32U);
    record->rng_domain_count = fd_load_u32_le(bytes + 224U);
    if (record->decision.seat_count != FD_U01_SEAT_COUNT ||
        record->decision.reserved != UINT32_C(0) ||
        record->decision.expected_tick != record->tick_before ||
        record->rng_domain_count != UINT32_C(0) ||
        memcmp(record->decision.expected_pre_state_sha256,
               record->pre_hash, 32U) != 0 ||
        record->tick_before > UINT64_MAX - FD_OPERATIONAL_TICKS ||
        record->tick_before % FD_OPERATIONAL_TICKS != UINT64_C(0) ||
        record->tick_after != record->tick_before + FD_OPERATIONAL_TICKS ||
        (index > UINT64_C(0) &&
         record->tick_before < FD_OPERATIONAL_TICKS)) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                           (uint32_t)index, "noncanonical replay decision record");
    }
    return FD_OK;
}

static void fd_encode_joint_wire(uint8_t *bytes,
                                 const fd_joint_decision *decision)
{
    uint32_t seat;
    fd_store_u32_le(bytes, decision->struct_size);
    fd_store_u32_le(bytes + 4U, decision->abi_version);
    fd_store_u32_le(bytes + 8U, decision->seat_count);
    fd_store_u32_le(bytes + 12U, decision->reserved);
    fd_store_u64_le(bytes + 16U, decision->expected_tick);
    memcpy(bytes + 24U, decision->expected_pre_state_sha256, 32U);
    for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        uint8_t *seat_bytes = bytes + 56U + (uint64_t)seat * UINT64_C(40);
        uint32_t parameter;
        fd_store_u64_le(seat_bytes, decision->seats[seat].actor_id);
        fd_store_u32_le(seat_bytes + 8U,
                        decision->seats[seat].action.struct_size);
        fd_store_u32_le(seat_bytes + 12U,
                        decision->seats[seat].action.abi_version);
        fd_store_u32_le(seat_bytes + 16U,
                        decision->seats[seat].action.category);
        fd_store_u32_le(seat_bytes + 20U,
                        decision->seats[seat].action.parameter_count);
        for (parameter = 0U; parameter < 4U; ++parameter) {
            fd_store_u32_le(seat_bytes + 24U + (uint64_t)parameter * UINT64_C(4),
                            decision->seats[seat].action.parameters[parameter]);
        }
    }
}

static void fd_decode_joint_wire(const uint8_t *bytes,
                                 fd_joint_decision *decision)
{
    uint32_t seat;
    memset(decision, 0, sizeof(*decision));
    decision->struct_size = fd_load_u32_le(bytes);
    decision->abi_version = fd_load_u32_le(bytes + 4U);
    decision->seat_count = fd_load_u32_le(bytes + 8U);
    decision->reserved = fd_load_u32_le(bytes + 12U);
    decision->expected_tick = fd_load_u64_le(bytes + 16U);
    memcpy(decision->expected_pre_state_sha256, bytes + 24U, 32U);
    for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
        const uint8_t *seat_bytes = bytes + 56U + (uint64_t)seat * UINT64_C(40);
        uint32_t parameter;
        decision->seats[seat].actor_id = fd_load_u64_le(seat_bytes);
        decision->seats[seat].action.struct_size =
            fd_load_u32_le(seat_bytes + 8U);
        decision->seats[seat].action.abi_version =
            fd_load_u32_le(seat_bytes + 12U);
        decision->seats[seat].action.category = fd_load_u32_le(seat_bytes + 16U);
        decision->seats[seat].action.parameter_count =
            fd_load_u32_le(seat_bytes + 20U);
        for (parameter = 0U; parameter < 4U; ++parameter) {
            decision->seats[seat].action.parameters[parameter] =
                fd_load_u32_le(seat_bytes + 24U +
                               (uint64_t)parameter * UINT64_C(4));
        }
    }
}

static void fd_encode_audit(uint8_t *bytes,
                            uint64_t attempt_index,
                            uint64_t tick,
                            const fd_joint_decision *decision,
                            const fd_diagnostic *diagnostic)
{
    uint32_t message_length = UINT32_C(0);
    while (message_length + UINT32_C(1) <
               (uint32_t)sizeof(diagnostic->message) &&
           diagnostic->message[message_length] != '\0') {
        ++message_length;
    }
    fd_store_u64_le(bytes, attempt_index);
    fd_store_u64_le(bytes + 8U, tick);
    fd_encode_joint_wire(bytes + 16U, decision);
    fd_store_u32_le(bytes + 152U, diagnostic->code);
    fd_store_u32_le(bytes + 156U, diagnostic->field_id);
    fd_store_u32_le(bytes + 160U, diagnostic->item_index);
    fd_store_u32_le(bytes + 164U, message_length);
    memset(bytes + 168U, 0, 160U);
    memcpy(bytes + 168U, diagnostic->message, (size_t)message_length);
}

static fd_result fd_decode_audit(const uint8_t *bytes,
                                 uint64_t total_attempts,
                                 uint64_t previous_attempt,
                                 fd_replay_audit_internal *audit,
                                 fd_diagnostic *diag)
{
    uint32_t index;
    memset(audit, 0, sizeof(*audit));
    audit->attempt_index = fd_load_u64_le(bytes);
    audit->tick = fd_load_u64_le(bytes + 8U);
    fd_decode_joint_wire(bytes + 16U, &audit->decision);
    audit->result = fd_load_u32_le(bytes + 152U);
    audit->field_id = fd_load_u32_le(bytes + 156U);
    audit->item_index = fd_load_u32_le(bytes + 160U);
    audit->message_length = fd_load_u32_le(bytes + 164U);
    if (audit->attempt_index >= total_attempts ||
        (previous_attempt != UINT64_MAX &&
         audit->attempt_index <= previous_attempt) ||
        audit->tick % FD_OPERATIONAL_TICKS != UINT64_C(0) ||
        !fd_replay_result_is_auditable(audit->result) ||
        audit->field_id > FD_FIELD_ASCII_MESSAGE ||
        audit->message_length >= UINT32_C(160)) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                           (uint32_t)audit->attempt_index,
                           "noncanonical replay audit record");
    }
    for (index = 0U; index < audit->message_length; ++index) {
        uint8_t character = bytes[168U + index];
        if (character < UINT8_C(0x20) || character > UINT8_C(0x7e)) {
            return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                               (uint32_t)audit->attempt_index,
                               "invalid replay audit diagnostic text");
        }
    }
    memcpy(audit->message, bytes + 168U, (size_t)audit->message_length);
    for (index = audit->message_length; index < UINT32_C(160); ++index) {
        if (bytes[168U + index] != UINT8_C(0)) {
            return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                               (uint32_t)audit->attempt_index,
                               "nonzero replay audit padding");
        }
    }
    return FD_OK;
}

static bool fd_replay_result_is_auditable(fd_result result)
{
    return result == FD_ERR_INVALID_ARGUMENT ||
           result == FD_ERR_INVALID_SIZE ||
           result == FD_ERR_OUT_OF_RANGE ||
           result == FD_ERR_CAPACITY ||
           result == FD_ERR_INVALID_ACTION;
}

static fd_result fd_replay_classify_attempts(
    const fd_world *tick_zero_world,
    const fd_joint_decision *decisions,
    uint64_t attempt_count,
    uint64_t *out_accepted_count,
    uint32_t *out_audit_count,
    fd_diagnostic *diag)
{
    fd_world *work = NULL;
    uint64_t accepted_count = UINT64_C(0);
    uint32_t audit_count = UINT32_C(0);
    uint64_t attempt_index;
    fd_result result;
    if (tick_zero_world == NULL || out_accepted_count == NULL ||
        out_audit_count == NULL ||
        (attempt_count > UINT64_C(0) && decisions == NULL)) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "invalid replay attempt input");
    }
    *out_accepted_count = UINT64_C(0);
    *out_audit_count = UINT32_C(0);
    if (attempt_count > tick_zero_world->context->max_file_bytes /
                            FD_REPLAY_RECORD_WIRE_SIZE ||
        attempt_count > UINT64_MAX / FD_OPERATIONAL_TICKS) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "replay attempt count exceeds bounds");
    }
    result = fd_world_clone_via_save(tick_zero_world, &work, diag);
    if (result != FD_OK) {
        return result;
    }
    for (attempt_index = UINT64_C(0); attempt_index < attempt_count;
         ++attempt_index) {
        fd_step_result step;
        fd_diagnostic attempt_diag;
        memset(&step, 0, sizeof(step));
        step.struct_size = (uint32_t)sizeof(step);
        step.abi_version = FD_ABI_VERSION;
        (void)fd_diagnostic_init(&attempt_diag);
        result = fd_world_step(work, &decisions[attempt_index], &step,
                               &attempt_diag);
        if (result == FD_OK) {
            ++accepted_count;
        } else if (fd_replay_result_is_auditable(result)) {
            if (audit_count == UINT32_MAX) {
                (void)fd_world_destroy(&work, NULL);
                return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_FILE_BYTES,
                                   UINT32_C(0),
                                   "replay audit count exceeds format bound");
            }
            ++audit_count;
        } else {
            (void)fd_world_destroy(&work, NULL);
            return fd_diag_set(diag, result, attempt_diag.field_id,
                               attempt_diag.item_index,
                               attempt_diag.message);
        }
    }
    (void)fd_world_destroy(&work, NULL);
    *out_accepted_count = accepted_count;
    *out_audit_count = audit_count;
    return FD_OK;
}

static fd_result fd_replay_size_internal(const fd_world *world,
                                         uint64_t accepted_count,
                                         uint32_t audit_count,
                                         uint64_t *out_size,
                                         uint64_t *out_save_size,
                                         uint32_t *out_checkpoints)
{
    uint64_t save_size;
    uint64_t records_length;
    uint64_t audits_length;
    uint64_t checkpoints_length;
    uint64_t checkpoint_count64 = accepted_count / UINT64_C(1024);
    uint64_t payload_total;
    uint64_t total;
    fd_result result;
    if (world == NULL || out_size == NULL || out_save_size == NULL ||
        out_checkpoints == NULL || checkpoint_count64 > (uint64_t)UINT32_MAX) {
        return FD_ERR_INVALID_ARGUMENT;
    }
    result = fd_world_save_size(world, &save_size, NULL);
    if (result != FD_OK ||
        !fd_u64_mul(accepted_count, FD_REPLAY_RECORD_WIRE_SIZE,
                    &records_length) ||
        !fd_u64_add(records_length, UINT64_C(8), &records_length) ||
        !fd_u64_mul((uint64_t)audit_count, FD_REPLAY_AUDIT_WIRE_SIZE,
                    &audits_length) ||
        !fd_u64_add(audits_length, UINT64_C(4), &audits_length)) {
        return FD_ERR_CAPACITY;
    }
    if (!fd_u64_add(save_size, UINT64_C(16), &checkpoints_length) ||
        !fd_u64_mul(checkpoint_count64, checkpoints_length,
                    &checkpoints_length) ||
        !fd_u64_add(checkpoints_length, UINT64_C(4),
                    &checkpoints_length)) {
        return FD_ERR_CAPACITY;
    }
    if (!fd_u64_add(UINT64_C(28), save_size, &payload_total) ||
        !fd_u64_add(payload_total, records_length, &payload_total) ||
        !fd_u64_add(payload_total, audits_length, &payload_total) ||
        !fd_u64_add(payload_total, checkpoints_length, &payload_total) ||
        !fd_u64_add(UINT64_C(8), payload_total, &total) ||
        !fd_u64_add(total,
                    (uint64_t)FD_REPLAY_SECTION_COUNT * UINT64_C(48),
                    &total) ||
        !fd_u64_add(total, UINT64_C(32), &total)) {
        return FD_ERR_CAPACITY;
    }
    *out_size = total;
    *out_save_size = save_size;
    *out_checkpoints = (uint32_t)checkpoint_count64;
    return FD_OK;
}

fd_result fd_replay_build_size(const fd_world *tick_zero_world,
                               const fd_joint_decision *decisions,
                               uint64_t decision_count,
                               uint64_t *out_size,
                               fd_diagnostic *diag)
{
    uint64_t save_size;
    uint64_t accepted_count;
    uint32_t audit_count;
    uint32_t checkpoints;
    fd_result result;
    if (tick_zero_world == NULL || out_size == NULL ||
        (decision_count > UINT64_C(0) && decisions == NULL)) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "invalid replay build size input");
    }
    if (tick_zero_world->tick != UINT64_C(0)) {
        return fd_diag_set(diag, FD_ERR_STATE, FD_FIELD_TICK,
                           UINT32_C(0), "replay source must be tick zero");
    }
    result = fd_replay_classify_attempts(tick_zero_world, decisions,
                                         decision_count, &accepted_count,
                                         &audit_count, diag);
    if (result != FD_OK) {
        return result;
    }
    result = fd_replay_size_internal(tick_zero_world, accepted_count,
                                     audit_count, out_size, &save_size,
                                     &checkpoints);
    (void)save_size;
    (void)checkpoints;
    if (result != FD_OK || *out_size > tick_zero_world->context->max_file_bytes) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "replay exceeds configured file bound");
    }
    return fd_diag_ok(diag);
}

static fd_result fd_replay_build_into(const fd_world *tick_zero_world,
                                      const fd_joint_decision *decisions,
                                      uint64_t decision_count,
                                      void *buffer,
                                      uint64_t capacity,
                                      uint64_t *written,
                                      fd_diagnostic *diag)
{
    uint8_t *bytes = (uint8_t *)buffer;
    uint64_t required;
    uint64_t save_size;
    uint64_t accepted_count;
    uint32_t audit_count;
    uint32_t checkpoint_count;
    uint64_t position = UINT64_C(0);
    uint64_t payload1;
    uint64_t payload2;
    uint64_t payload3;
    uint64_t payload4;
    uint64_t payload5;
    uint64_t records_length;
    uint64_t checkpoints_length;
    uint64_t save_written = UINT64_C(0);
    uint64_t record_cursor;
    uint64_t audit_cursor;
    uint64_t checkpoint_cursor;
    uint64_t decision_index;
    uint64_t accepted_index = UINT64_C(0);
    fd_world *work = NULL;
    fd_result result;
    uint8_t footer[32];
    if (written == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay written output is null");
    }
    *written = UINT64_C(0);
    if (tick_zero_world == NULL ||
        (decision_count > UINT64_C(0) && decisions == NULL)) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "invalid replay build input");
    }
    if (tick_zero_world->tick != UINT64_C(0)) {
        return fd_diag_set(diag, FD_ERR_STATE, FD_FIELD_TICK,
                           UINT32_C(0), "replay source must be tick zero");
    }
    result = fd_replay_classify_attempts(tick_zero_world, decisions,
                                         decision_count, &accepted_count,
                                         &audit_count, diag);
    if (result != FD_OK) {
        return result;
    }
    result = fd_replay_size_internal(tick_zero_world, accepted_count,
                                     audit_count, &required, &save_size,
                                     &checkpoint_count);
    if (result != FD_OK || required > tick_zero_world->context->max_file_bytes) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "replay exceeds configured file bound");
    }
    if (bytes == NULL || capacity < required) {
        return fd_diag_set(diag, FD_ERR_BUFFER_TOO_SMALL,
                           FD_FIELD_BUFFER_CAPACITY, UINT32_C(0),
                           "replay buffer is smaller than required size");
    }
    records_length = UINT64_C(8) +
                     accepted_count * FD_REPLAY_RECORD_WIRE_SIZE;
    checkpoints_length = UINT64_C(4) +
                         (uint64_t)checkpoint_count *
                         (UINT64_C(16) + save_size);
    memset(bytes, 0, (size_t)required);
    memcpy(bytes, fd_replay_magic, sizeof(fd_replay_magic));
    position = UINT64_C(8);
    fd_replay_write_section_header(bytes, position, UINT16_C(1), UINT64_C(28));
    payload1 = position + UINT64_C(16);
    fd_store_u16_le(bytes + payload1, FD_REPLAY_VERSION_MAJOR);
    fd_store_u16_le(bytes + payload1 + UINT64_C(2), FD_REPLAY_VERSION_MINOR);
    fd_store_u16_le(bytes + payload1 + UINT64_C(4), FD_REPLAY_VERSION_MAJOR);
    fd_store_u16_le(bytes + payload1 + UINT64_C(6), UINT16_C(0));
    fd_store_u32_le(bytes + payload1 + UINT64_C(8), FD_REPLAY_SECTION_COUNT);
    fd_store_u64_le(bytes + payload1 + UINT64_C(12), accepted_count);
    fd_store_u32_le(bytes + payload1 + UINT64_C(20), checkpoint_count);
    fd_store_u32_le(bytes + payload1 + UINT64_C(24), audit_count);
    fd_replay_finish_section(bytes, payload1, UINT64_C(28));
    position = payload1 + UINT64_C(28) + UINT64_C(32);

    fd_replay_write_section_header(bytes, position, UINT16_C(2), save_size);
    payload2 = position + UINT64_C(16);
    result = fd_world_save(tick_zero_world, bytes + payload2, save_size,
                           &save_written, diag);
    if (result != FD_OK || save_written != save_size) {
        return result == FD_OK ? FD_ERR_INTERNAL : result;
    }
    fd_replay_finish_section(bytes, payload2, save_size);
    position = payload2 + save_size + UINT64_C(32);

    fd_replay_write_section_header(bytes, position, UINT16_C(3), records_length);
    payload3 = position + UINT64_C(16);
    fd_store_u64_le(bytes + payload3, accepted_count);
    record_cursor = payload3 + UINT64_C(8);
    position = payload3 + records_length + UINT64_C(32);

    fd_replay_write_section_header(
        bytes, position, UINT16_C(4),
        UINT64_C(4) + (uint64_t)audit_count * FD_REPLAY_AUDIT_WIRE_SIZE);
    payload4 = position + UINT64_C(16);
    fd_store_u32_le(bytes + payload4, audit_count);
    audit_cursor = payload4 + UINT64_C(4);
    position = audit_cursor +
               (uint64_t)audit_count * FD_REPLAY_AUDIT_WIRE_SIZE +
               UINT64_C(32);

    fd_replay_write_section_header(bytes, position, UINT16_C(5), checkpoints_length);
    payload5 = position + UINT64_C(16);
    fd_store_u32_le(bytes + payload5, checkpoint_count);
    checkpoint_cursor = payload5 + UINT64_C(4);
    position = payload5 + checkpoints_length + UINT64_C(32);
    if (position + UINT64_C(32) != required) {
        return fd_diag_set(diag, FD_ERR_INTERNAL, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "replay layout size mismatch");
    }
    result = fd_world_clone_via_save(tick_zero_world, &work, diag);
    if (result != FD_OK) {
        return result;
    }
    for (decision_index = 0U; decision_index < decision_count; ++decision_index) {
        fd_replay_record_internal record;
        fd_step_result step;
        fd_diagnostic attempt_diag;
        memset(&record, 0, sizeof(record));
        memset(&step, 0, sizeof(step));
        step.struct_size = (uint32_t)sizeof(step);
        step.abi_version = FD_ABI_VERSION;
        (void)fd_diagnostic_init(&attempt_diag);
        record.tick_before = work->tick;
        memcpy(record.pre_hash, work->state_sha256, 32U);
        result = fd_world_step(work, &decisions[decision_index], &step,
                               &attempt_diag);
        if (result != FD_OK && fd_replay_result_is_auditable(result)) {
            fd_encode_audit(bytes + audit_cursor, decision_index, work->tick,
                            &decisions[decision_index], &attempt_diag);
            audit_cursor += FD_REPLAY_AUDIT_WIRE_SIZE;
            continue;
        }
        if (result != FD_OK) {
            (void)fd_world_destroy(&work, NULL);
            return fd_diag_set(diag, result, attempt_diag.field_id,
                               attempt_diag.item_index,
                               attempt_diag.message);
        }
        record.tick_before = step.staged_tick_before;
        record.tick_after = step.staged_tick_after;
        record.decision = step.staged_decision;
        record.rng_domain_count = step.rng_domain_count;
        memcpy(record.pre_hash, step.staged_pre_state_sha256, 32U);
        memcpy(record.post_hash, step.finalized_post_state_sha256, 32U);
        memcpy(record.event_hash, step.staged_event_sha256, 32U);
        {
            uint8_t staged_decision_hash[32];
            fd_hash_joint_decision_internal(&record.decision,
                                            staged_decision_hash);
            if (step.transition_stage_phase != UINT32_C(22) ||
                step.state_hash_phase != UINT32_C(23) ||
                step.transition_stage_trace_index >=
                    step.state_hash_trace_index ||
                memcmp(staged_decision_hash,
                       step.staged_decision_sha256, 32U) != 0 ||
                memcmp(step.post_state_sha256,
                       step.finalized_post_state_sha256, 32U) != 0) {
                (void)fd_world_destroy(&work, NULL);
                return fd_diag_set(diag, FD_ERR_INTERNAL, FD_FIELD_ACTION,
                                   (uint32_t)decision_index,
                                   "phase-22 replay staging metadata mismatch");
            }
        }
        fd_encode_replay_record(bytes + record_cursor, &record);
        record_cursor += FD_REPLAY_RECORD_WIRE_SIZE;
        ++accepted_index;
        if (accepted_index % UINT64_C(1024) == UINT64_C(0)) {
            uint64_t checkpoint_written = UINT64_C(0);
            fd_store_u64_le(bytes + checkpoint_cursor, accepted_index);
            fd_store_u64_le(bytes + checkpoint_cursor + UINT64_C(8), save_size);
            result = fd_world_save(work, bytes + checkpoint_cursor + UINT64_C(16),
                                   save_size, &checkpoint_written, diag);
            if (result != FD_OK || checkpoint_written != save_size) {
                (void)fd_world_destroy(&work, NULL);
                return result == FD_OK ? FD_ERR_INTERNAL : result;
            }
            checkpoint_cursor += UINT64_C(16) + save_size;
        }
    }
    (void)fd_world_destroy(&work, NULL);
    if (record_cursor != payload3 + records_length ||
        audit_cursor != payload4 + UINT64_C(4) +
                            (uint64_t)audit_count * FD_REPLAY_AUDIT_WIRE_SIZE ||
        checkpoint_cursor != payload5 + checkpoints_length) {
        return fd_diag_set(diag, FD_ERR_INTERNAL, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "replay record cursor mismatch");
    }
    fd_replay_finish_section(bytes, payload3, records_length);
    fd_replay_finish_section(bytes, payload4,
                             UINT64_C(4) +
                             (uint64_t)audit_count * FD_REPLAY_AUDIT_WIRE_SIZE);
    fd_replay_finish_section(bytes, payload5, checkpoints_length);
    fd_replay_footer_digest(bytes, required - UINT64_C(32), footer);
    memcpy(bytes + required - UINT64_C(32), footer, 32U);
    *written = required;
    return fd_diag_ok(diag);
}

fd_result fd_replay_build(const fd_world *tick_zero_world,
                          const fd_joint_decision *decisions,
                          uint64_t decision_count,
                          void *buffer,
                          uint64_t capacity,
                          uint64_t *written,
                          fd_diagnostic *diag)
{
    uint64_t required = UINT64_C(0);
    uint64_t temporary_written = UINT64_C(0);
    uint8_t *temporary;
    fd_result result;
    if (written == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay written output is null");
    }
    *written = UINT64_C(0);
    result = fd_replay_build_size(tick_zero_world, decisions, decision_count,
                                  &required, diag);
    if (result != FD_OK) {
        return result;
    }
    if (buffer == NULL || capacity < required) {
        return fd_diag_set(diag, FD_ERR_BUFFER_TOO_SMALL,
                           FD_FIELD_BUFFER_CAPACITY, UINT32_C(0),
                           "replay buffer is smaller than required size");
    }
    temporary = (uint8_t *)fd_context_alloc(tick_zero_world->context, required);
    if (temporary == NULL) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "transactional replay allocation failed");
    }
    result = fd_replay_build_into(tick_zero_world, decisions, decision_count,
                                  temporary, required, &temporary_written, diag);
    if (result == FD_OK && temporary_written == required) {
        memcpy(buffer, temporary, (size_t)required);
        *written = required;
    } else if (result == FD_OK) {
        result = fd_diag_set(diag, FD_ERR_INTERNAL, FD_FIELD_FILE_BYTES,
                             UINT32_C(0), "replay build size mismatch");
    }
    fd_context_free(tick_zero_world->context, temporary, required);
    return result;
}

static fd_result fd_parse_replay_sections(const uint8_t *bytes,
                                          uint64_t size,
                                          fd_replay_section_internal sections[6],
                                          uint32_t *out_count,
                                          fd_diagnostic *diag)
{
    uint64_t position = UINT64_C(8);
    uint64_t footer = size - UINT64_C(32);
    uint16_t previous = UINT16_C(0);
    uint32_t count = UINT32_C(0);
    memset(sections, 0, 6U * sizeof(sections[0]));
    while (position < footer) {
        uint16_t tag;
        uint16_t flags;
        uint32_t reserved;
        uint64_t length;
        uint64_t framed;
        uint8_t digest[32];
        const uint8_t *payload;
        if (footer - position < UINT64_C(48)) {
            return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                               count, "truncated replay section");
        }
        tag = (uint16_t)((uint16_t)bytes[position] |
                         (uint16_t)((uint16_t)bytes[position + 1U] << 8U));
        flags = (uint16_t)((uint16_t)bytes[position + 2U] |
                           (uint16_t)((uint16_t)bytes[position + 3U] << 8U));
        reserved = fd_load_u32_le(bytes + position + UINT64_C(4));
        length = fd_load_u64_le(bytes + position + UINT64_C(8));
        if (tag <= previous || reserved != UINT32_C(0) ||
            (flags & (uint16_t)~FD_SECTION_CRITICAL) != UINT16_C(0) ||
            !fd_u64_add(UINT64_C(48), length, &framed) || framed > footer - position) {
            return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                               count, "noncanonical replay section framing");
        }
        payload = bytes + position + UINT64_C(16);
        (void)fd_sha256(payload, length, digest);
        if (memcmp(digest, payload + length, 32U) != 0) {
            return fd_diag_set(diag, FD_ERR_CHECKSUM, FD_FIELD_SECTION,
                               count, "replay section digest mismatch");
        }
        if (tag <= UINT16_C(5)) {
            if ((flags & FD_SECTION_CRITICAL) == UINT16_C(0)) {
                return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                                   count, "mandatory replay section is optional");
            }
            sections[tag].payload = payload;
            sections[tag].length = length;
            sections[tag].present = true;
        } else if ((flags & FD_SECTION_CRITICAL) != UINT16_C(0)) {
            return fd_diag_set(diag, FD_ERR_VERSION, FD_FIELD_SECTION,
                               count, "unknown critical replay section");
        }
        previous = tag;
        position += framed;
        ++count;
        if (count > UINT32_C(64)) {
            return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_SECTION,
                               count, "too many replay sections");
        }
    }
    if (position != footer) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "unframed replay trailing bytes");
    }
    {
        uint32_t tag;
        for (tag = 1U; tag <= 5U; ++tag) {
            if (!sections[tag].present) {
                return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                                   tag, "missing replay section");
            }
        }
    }
    *out_count = count;
    return FD_OK;
}

fd_result fd_replay_open(fd_context *context,
                         const void *bytes_input,
                         uint64_t size,
                         fd_replay **out,
                         fd_diagnostic *diag)
{
    const uint8_t *bytes = (const uint8_t *)bytes_input;
    fd_replay_section_internal sections[6];
    uint32_t parsed_count = UINT32_C(0);
    uint8_t digest[32];
    uint64_t decision_count;
    uint32_t checkpoint_count;
    uint32_t audit_count;
    uint64_t records_length;
    uint64_t record_bytes;
    uint64_t audit_bytes;
    uint64_t audit_length;
    uint64_t total_attempts;
    fd_replay *replay;
    uint64_t index;
    uint32_t failure_section = UINT32_C(0);
    fd_world *check_world = NULL;
    fd_entity_id foreign_actor_ids[FD_U01_SEAT_COUNT] = {UINT64_C(0), UINT64_C(0)};
    fd_result result;
    if (out == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay output is null");
    }
    *out = NULL;
    if (context == NULL || bytes == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "context or replay bytes are null");
    }
    (void)fd_diag_ok(diag);
    if (size > context->max_file_bytes) {
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "replay exceeds configured file bound");
    }
    if (size < UINT64_C(8) + UINT64_C(32) + UINT64_C(48) ||
        memcmp(bytes, fd_replay_magic, sizeof(fd_replay_magic)) != 0) {
        if (size >= UINT64_C(8) && memcmp(bytes, fd_replay_magic, 7U) == 0) {
            return fd_diag_set(diag, FD_ERR_VERSION, FD_FIELD_FORMAT_VERSION,
                               UINT32_C(0), "unsupported replay major version");
        }
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "invalid or truncated replay magic");
    }
    fd_replay_footer_digest(bytes, size - UINT64_C(32), digest);
    if (memcmp(digest, bytes + size - UINT64_C(32), 32U) != 0) {
        return fd_diag_set(diag, FD_ERR_CHECKSUM, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "replay footer digest mismatch");
    }
    result = fd_parse_replay_sections(bytes, size, sections, &parsed_count, diag);
    if (result != FD_OK) {
        return result;
    }
    if (sections[1].length < UINT64_C(28) ||
        (uint16_t)((uint16_t)sections[1].payload[0] |
                   (uint16_t)((uint16_t)sections[1].payload[1] << 8U)) !=
            FD_REPLAY_VERSION_MAJOR ||
        (uint16_t)((uint16_t)sections[1].payload[4] |
                   (uint16_t)((uint16_t)sections[1].payload[5] << 8U)) !=
            FD_REPLAY_VERSION_MAJOR ||
        (uint16_t)((uint16_t)sections[1].payload[6] |
                   (uint16_t)((uint16_t)sections[1].payload[7] << 8U)) >
            FD_REPLAY_VERSION_MINOR ||
        fd_load_u32_le(sections[1].payload + 8U) != parsed_count) {
        return fd_diag_set(diag, FD_ERR_VERSION, FD_FIELD_FORMAT_VERSION,
                           UINT32_C(0), "unsupported replay manifest");
    }
    decision_count = fd_load_u64_le(sections[1].payload + 12U);
    checkpoint_count = fd_load_u32_le(sections[1].payload + 20U);
    audit_count = fd_load_u32_le(sections[1].payload + 24U);
    if (!fd_u64_mul(decision_count, FD_REPLAY_RECORD_WIRE_SIZE, &records_length) ||
        !fd_u64_add(records_length, UINT64_C(8), &records_length) ||
        !fd_u64_mul((uint64_t)audit_count, FD_REPLAY_AUDIT_WIRE_SIZE,
                    &audit_length) ||
        !fd_u64_add(audit_length, UINT64_C(4), &audit_length) ||
        !fd_u64_add(decision_count, (uint64_t)audit_count, &total_attempts) ||
        sections[3].length != records_length ||
        fd_load_u64_le(sections[3].payload) != decision_count ||
        sections[4].length != audit_length ||
        fd_load_u32_le(sections[4].payload) != audit_count ||
        sections[5].length < UINT64_C(4) ||
        fd_load_u32_le(sections[5].payload) != checkpoint_count ||
        decision_count / UINT64_C(1024) > (uint64_t)UINT32_MAX ||
        checkpoint_count != (uint32_t)(decision_count / UINT64_C(1024))) {
        return fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_SECTION,
                           UINT32_C(0), "replay counts or lengths are inconsistent");
    }
    result = fd_world_load(context, sections[2].payload, sections[2].length,
                           &check_world, diag);
    if (result != FD_OK) {
        return result;
    }
    if (check_world->tick != UINT64_C(0)) {
        (void)fd_world_destroy(&check_world, NULL);
        return fd_diag_set(diag, FD_ERR_STATE, FD_FIELD_TICK,
                           UINT32_C(0), "replay checkpoint is not tick zero");
    }
    if (!fd_u64_mul(decision_count,
                    (uint64_t)sizeof(fd_replay_record_internal), &record_bytes) ||
        !fd_u64_mul((uint64_t)audit_count,
                    (uint64_t)sizeof(fd_replay_audit_internal), &audit_bytes)) {
        (void)fd_world_destroy(&check_world, NULL);
        return fd_diag_set(diag, FD_ERR_CAPACITY, FD_FIELD_FILE_BYTES,
                           UINT32_C(0), "replay record allocation size overflows");
    }
    replay = (fd_replay *)fd_context_alloc(context, (uint64_t)sizeof(*replay));
    if (replay == NULL) {
        (void)fd_world_destroy(&check_world, NULL);
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "replay handle allocation failed");
    }
    memset(replay, 0, sizeof(*replay));
    replay->tick_zero_save = (uint8_t *)fd_context_alloc(context, sections[2].length);
    if (replay->tick_zero_save == NULL) {
        fd_context_free(context, replay, (uint64_t)sizeof(*replay));
        (void)fd_world_destroy(&check_world, NULL);
        return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                           UINT32_C(0), "replay checkpoint allocation failed");
    }
    if (record_bytes > UINT64_C(0)) {
        replay->records = (fd_replay_record_internal *)
            fd_context_alloc(context, record_bytes);
        if (replay->records == NULL) {
            fd_context_free(context, replay->tick_zero_save, sections[2].length);
            fd_context_free(context, replay, (uint64_t)sizeof(*replay));
            (void)fd_world_destroy(&check_world, NULL);
            return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                               UINT32_C(0), "replay record allocation failed");
        }
    }
    if (audit_bytes > UINT64_C(0)) {
        replay->audits = (fd_replay_audit_internal *)
            fd_context_alloc(context, audit_bytes);
        if (replay->audits == NULL) {
            fd_context_free(context, replay->records, record_bytes);
            fd_context_free(context, replay->tick_zero_save, sections[2].length);
            fd_context_free(context, replay, (uint64_t)sizeof(*replay));
            (void)fd_world_destroy(&check_world, NULL);
            return fd_diag_set(diag, FD_ERR_OUT_OF_MEMORY, FD_FIELD_NONE,
                               UINT32_C(0), "replay audit allocation failed");
        }
    }
    replay->context = context;
    replay->tick_zero_save_size = sections[2].length;
    replay->tick_zero_allocation_size = sections[2].length;
    replay->record_count = decision_count;
    replay->record_allocation_size = record_bytes;
    replay->audit_count = audit_count;
    replay->audit_allocation_size = audit_bytes;
    replay->checkpoint_count = checkpoint_count;
    memcpy(replay->tick_zero_save, sections[2].payload, (size_t)sections[2].length);
    memcpy(replay->tick_zero_hash, check_world->state_sha256, 32U);
    foreign_actor_ids[0] = check_world->region.foreign_faction_ids[0];
    foreign_actor_ids[1] = check_world->region.foreign_faction_ids[1];
    (void)fd_world_destroy(&check_world, NULL);
    for (index = 0U; index < decision_count; ++index) {
        result = fd_decode_replay_record(
            sections[3].payload + UINT64_C(8) +
            index * FD_REPLAY_RECORD_WIRE_SIZE,
            index, &replay->records[index], diag);
        if (result != FD_OK ||
            replay->records[index].decision.seats[0].actor_id != foreign_actor_ids[0] ||
            replay->records[index].decision.seats[1].actor_id != foreign_actor_ids[1] ||
            replay->records[index].decision.seats[0].action.category != FD_ACTION_PASS ||
            replay->records[index].decision.seats[1].action.category != FD_ACTION_PASS ||
            replay->records[index].decision.seats[0].action.parameter_count != UINT32_C(0) ||
            replay->records[index].decision.seats[1].action.parameter_count != UINT32_C(0) ||
            (index == UINT64_C(0) && replay->records[index].tick_before != UINT64_C(0)) ||
            (index == UINT64_C(0) &&
             memcmp(replay->records[index].pre_hash,
                    replay->tick_zero_hash, 32U) != 0) ||
            (index > UINT64_C(0) &&
             (replay->records[index].tick_before !=
                  replay->records[index - UINT64_C(1)].tick_after ||
              memcmp(replay->records[index].pre_hash,
                     replay->records[index - UINT64_C(1)].post_hash, 32U) != 0))) {
            fd_context_free(context, replay->audits, audit_bytes);
            fd_context_free(context, replay->records, record_bytes);
            fd_context_free(context, replay->tick_zero_save, sections[2].length);
            fd_context_free(context, replay, (uint64_t)sizeof(*replay));
            return result != FD_OK ? result :
                fd_diag_set(diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                            (uint32_t)index, "replay record chain is discontinuous");
        }
        {
            uint32_t seat;
            for (seat = 0U; seat < FD_U01_SEAT_COUNT; ++seat) {
                uint32_t parameter;
                for (parameter = 0U; parameter < 4U; ++parameter) {
                    if (replay->records[index].decision.seats[seat].action
                            .parameters[parameter] != UINT32_C(0)) {
                        result = fd_diag_set(
                            diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                            (uint32_t)index,
                            "section 3 contains nonzero PASS parameter");
                        failure_section = UINT32_C(3);
                        goto replay_open_fail;
                    }
                }
            }
        }
    }
    {
        uint64_t previous_attempt = UINT64_MAX;
        uint32_t audit_index;
        for (audit_index = 0U; audit_index < audit_count; ++audit_index) {
            fd_replay_audit_internal *audit = &replay->audits[audit_index];
            uint64_t accepted_before;
            result = fd_decode_audit(
                sections[4].payload + UINT64_C(4) +
                    (uint64_t)audit_index * FD_REPLAY_AUDIT_WIRE_SIZE,
                total_attempts, previous_attempt, audit, diag);
            if (result != FD_OK ||
                audit->attempt_index < (uint64_t)audit_index) {
                if (result == FD_OK) {
                    result = fd_diag_set(
                        diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                        (uint32_t)audit->attempt_index,
                        "section 4 audit attempt order is invalid");
                }
                failure_section = UINT32_C(4);
                goto replay_open_fail;
            }
            accepted_before = audit->attempt_index - (uint64_t)audit_index;
            if (accepted_before > decision_count ||
                accepted_before > UINT64_MAX / FD_OPERATIONAL_TICKS ||
                audit->tick != accepted_before * FD_OPERATIONAL_TICKS) {
                result = fd_diag_set(
                    diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                    (uint32_t)audit->attempt_index,
                    "section 4 audit tick is not chronological");
                failure_section = UINT32_C(4);
                goto replay_open_fail;
            }
            previous_attempt = audit->attempt_index;
        }
    }
    result = fd_world_load(context, replay->tick_zero_save,
                           replay->tick_zero_save_size, &check_world, diag);
    if (result != FD_OK) {
        failure_section = UINT32_C(2);
        goto replay_open_fail;
    }
    {
        uint64_t attempt_index;
        uint64_t accepted_index = UINT64_C(0);
        uint32_t audit_index = UINT32_C(0);
        for (attempt_index = UINT64_C(0); attempt_index < total_attempts;
             ++attempt_index) {
            fd_step_result step;
            fd_diagnostic attempt_diag;
            uint8_t pre_hash[32];
            uint64_t pre_tick = check_world->tick;
            memset(&step, 0, sizeof(step));
            step.struct_size = (uint32_t)sizeof(step);
            step.abi_version = FD_ABI_VERSION;
            (void)fd_diagnostic_init(&attempt_diag);
            memcpy(pre_hash, check_world->state_sha256, 32U);
            if (audit_index < audit_count &&
                replay->audits[audit_index].attempt_index == attempt_index) {
                const fd_replay_audit_internal *audit =
                    &replay->audits[audit_index];
                uint32_t message_length = UINT32_C(0);
                fd_result attempt_result = fd_world_step(
                    check_world, &audit->decision, &step, &attempt_diag);
                while (message_length + UINT32_C(1) <
                           (uint32_t)sizeof(attempt_diag.message) &&
                       attempt_diag.message[message_length] != '\0') {
                    ++message_length;
                }
                if (attempt_result != audit->result ||
                    attempt_diag.code != audit->result ||
                    attempt_diag.field_id != audit->field_id ||
                    attempt_diag.item_index != audit->item_index ||
                    message_length != audit->message_length ||
                    memcmp(attempt_diag.message, audit->message,
                           (size_t)message_length) != 0 ||
                    check_world->tick != pre_tick ||
                    memcmp(check_world->state_sha256, pre_hash, 32U) != 0) {
                    result = fd_diag_set(
                        diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                        (uint32_t)attempt_index,
                        "section 4 audit diagnostic failed semantic verification");
                    failure_section = UINT32_C(4);
                    break;
                }
                ++audit_index;
            } else {
                const fd_replay_record_internal *record;
                fd_result attempt_result;
                if (accepted_index >= decision_count) {
                    result = fd_diag_set(
                        diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                        (uint32_t)attempt_index,
                        "section 3 has more accepted attempts than records");
                    failure_section = UINT32_C(3);
                    break;
                }
                record = &replay->records[accepted_index];
                if (record->tick_before != check_world->tick ||
                    memcmp(record->pre_hash, check_world->state_sha256, 32U) != 0) {
                    result = fd_diag_set(
                        diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                        (uint32_t)attempt_index,
                        "section 3 transition pre-state does not match");
                    failure_section = UINT32_C(3);
                    break;
                }
                attempt_result = fd_world_step(check_world, &record->decision,
                                               &step, &attempt_diag);
                if (attempt_result != FD_OK ||
                    record->tick_after != check_world->tick ||
                    memcmp(record->post_hash, check_world->state_sha256, 32U) != 0 ||
                    memcmp(record->event_hash, step.event_sha256, 32U) != 0) {
                    result = fd_diag_set(
                        diag,
                        attempt_result == FD_OK ? FD_ERR_CHECKSUM :
                                                  attempt_result,
                        FD_FIELD_ACTION, (uint32_t)attempt_index,
                        "section 3 accepted transition failed semantic verification");
                    failure_section = UINT32_C(3);
                    break;
                }
                ++accepted_index;
            }
        }
        if (result == FD_OK &&
            (accepted_index != decision_count || audit_index != audit_count)) {
            result = fd_diag_set(
                diag, FD_ERR_FORMAT, FD_FIELD_ACTION,
                (uint32_t)(accepted_index + (uint64_t)audit_index),
                "replay attempt merge did not consume every record");
            failure_section = audit_index != audit_count ? UINT32_C(4) :
                                                          UINT32_C(3);
        }
    }
    (void)fd_world_destroy(&check_world, NULL);
    if (result != FD_OK) {
        goto replay_open_fail;
    }
    {
        uint64_t checkpoint_position = UINT64_C(4);
        uint32_t checkpoint;
        for (checkpoint = 0U; checkpoint < checkpoint_count; ++checkpoint) {
            uint64_t decision_offset;
            uint64_t checkpoint_size;
            if (sections[5].length - checkpoint_position < UINT64_C(16)) {
                result = fd_diag_set(
                    diag, FD_ERR_FORMAT, FD_FIELD_SECTION, checkpoint,
                    "section 5 checkpoint header is truncated");
                failure_section = UINT32_C(5);
                goto replay_open_fail;
            }
            decision_offset = fd_load_u64_le(sections[5].payload + checkpoint_position);
            checkpoint_size = fd_load_u64_le(sections[5].payload + checkpoint_position +
                                             UINT64_C(8));
            checkpoint_position += UINT64_C(16);
            if (decision_offset != (uint64_t)(checkpoint + UINT32_C(1)) * UINT64_C(1024) ||
                checkpoint_size > sections[5].length - checkpoint_position) {
                result = fd_diag_set(
                    diag, FD_ERR_FORMAT, FD_FIELD_SECTION, checkpoint,
                    "section 5 checkpoint offset or size is invalid");
                failure_section = UINT32_C(5);
                goto replay_open_fail;
            }
            result = fd_world_load(context, sections[5].payload + checkpoint_position,
                                   checkpoint_size, &check_world, diag);
            if (result != FD_OK) {
                failure_section = UINT32_C(5);
                goto replay_open_fail;
            }
            if (check_world->tick != decision_offset * FD_OPERATIONAL_TICKS ||
                memcmp(check_world->state_sha256,
                       replay->records[decision_offset - UINT64_C(1)].post_hash,
                       32U) != 0) {
                (void)fd_world_destroy(&check_world, NULL);
                result = fd_diag_set(
                    diag, FD_ERR_CHECKSUM, FD_FIELD_SECTION, checkpoint,
                    "section 5 checkpoint state does not match transition");
                failure_section = UINT32_C(5);
                goto replay_open_fail;
            }
            (void)fd_world_destroy(&check_world, NULL);
            checkpoint_position += checkpoint_size;
        }
        if (checkpoint_position != sections[5].length) {
            result = fd_diag_set(
                diag, FD_ERR_FORMAT, FD_FIELD_SECTION, checkpoint_count,
                "section 5 checkpoint bytes are not fully consumed");
            failure_section = UINT32_C(5);
            goto replay_open_fail;
        }
    }
    (void)atomic_fetch_add_explicit(&context->live_handles, UINT32_C(1),
                                    memory_order_release);
    *out = replay;
    return fd_diag_ok(diag);

replay_open_fail:
    fd_context_free(context, replay->audits, audit_bytes);
    fd_context_free(context, replay->records, record_bytes);
    fd_context_free(context, replay->tick_zero_save, sections[2].length);
    fd_context_free(context, replay, (uint64_t)sizeof(*replay));
    if (diag != NULL && diag->struct_size >= (uint32_t)sizeof(*diag) &&
        diag->abi_version == FD_ABI_VERSION && diag->code == result &&
        result != FD_OK) {
        return result;
    }
    if (failure_section == UINT32_C(4)) {
        return fd_diag_set(diag, result, FD_FIELD_SECTION, failure_section,
                           "replay audit validation failed");
    }
    if (failure_section == UINT32_C(3)) {
        return fd_diag_set(diag, result, FD_FIELD_SECTION, failure_section,
                           "replay transition validation failed");
    }
    if (failure_section == UINT32_C(2)) {
        return fd_diag_set(diag, result, FD_FIELD_SECTION, failure_section,
                           "replay tick-zero checkpoint validation failed");
    }
    return fd_diag_set(diag, result, FD_FIELD_SECTION, UINT32_C(5),
                       "replay checkpoint validation failed");
}

fd_result fd_replay_destroy(fd_replay **replay_ptr, fd_diagnostic *diag)
{
    fd_replay *replay;
    fd_context *context;
    if (replay_ptr == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay pointer is null");
    }
    replay = *replay_ptr;
    if (replay == NULL) {
        return fd_diag_ok(diag);
    }
    context = replay->context;
    fd_context_free(context, replay->audits, replay->audit_allocation_size);
    fd_context_free(context, replay->records, replay->record_allocation_size);
    fd_context_free(context, replay->tick_zero_save,
                    replay->tick_zero_allocation_size);
    *replay_ptr = NULL;
    fd_context_free(context, replay, (uint64_t)sizeof(*replay));
    (void)atomic_fetch_sub_explicit(&context->live_handles, UINT32_C(1),
                                    memory_order_release);
    return fd_diag_ok(diag);
}

fd_result fd_replay_get_info(const fd_replay *replay,
                             fd_replay_info *out,
                             fd_diagnostic *diag)
{
    if (replay == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid replay info output");
    }
    out->decision_count = replay->record_count;
    out->cursor = replay->cursor;
    out->tick_zero_save_bytes = replay->tick_zero_save_size;
    out->checkpoint_count = replay->checkpoint_count;
    out->audit_count = replay->audit_count;
    memcpy(out->tick_zero_state_sha256, replay->tick_zero_hash, 32U);
    return fd_diag_ok(diag);
}

fd_result fd_replay_get_record(const fd_replay *replay,
                               uint64_t index,
                               fd_replay_record_view *out,
                               fd_diagnostic *diag)
{
    const fd_replay_record_internal *record;
    if (replay == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid replay record output");
    }
    if (index >= replay->record_count) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ACTION,
                           (uint32_t)index, "replay record index is out of range");
    }
    record = &replay->records[index];
    out->decision_index = index;
    out->tick_before = record->tick_before;
    out->tick_after = record->tick_after;
    out->decision = record->decision;
    out->rng_domain_count = record->rng_domain_count;
    out->reserved = UINT32_C(0);
    memcpy(out->pre_state_sha256, record->pre_hash, 32U);
    memcpy(out->post_state_sha256, record->post_hash, 32U);
    memcpy(out->event_sha256, record->event_hash, 32U);
    return fd_diag_ok(diag);
}

fd_result fd_replay_get_audit(const fd_replay *replay,
                              uint32_t index,
                              fd_replay_audit_view *out,
                              fd_diagnostic *diag)
{
    const fd_replay_audit_internal *audit;
    if (replay == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid replay audit output");
    }
    if (index >= replay->audit_count) {
        return fd_diag_set(diag, FD_ERR_OUT_OF_RANGE, FD_FIELD_ACTION,
                           index, "replay audit index is out of range");
    }
    audit = &replay->audits[index];
    out->attempt_index = audit->attempt_index;
    out->tick = audit->tick;
    out->decision = audit->decision;
    out->result = audit->result;
    out->field_id = audit->field_id;
    out->item_index = audit->item_index;
    out->message_length = audit->message_length;
    memset(out->message, 0, sizeof(out->message));
    memcpy(out->message, audit->message, (size_t)audit->message_length);
    return fd_diag_ok(diag);
}

fd_result fd_replay_create_world(const fd_replay *replay,
                                 fd_world **out,
                                 fd_diagnostic *diag)
{
    if (out == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay world output is null");
    }
    *out = NULL;
    if (replay == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay is null");
    }
    return fd_world_load(replay->context, replay->tick_zero_save,
                         replay->tick_zero_save_size, out, diag);
}

fd_result fd_replay_advance(fd_replay *replay,
                            fd_world *world,
                            uint64_t decisions,
                            fd_replay_status *out,
                            fd_diagnostic *diag)
{
    fd_replay_status status;
    uint64_t remaining;
    uint64_t count;
    uint64_t index;
    uint64_t expected_tick;
    const uint8_t *expected_hash;
    fd_world *candidate = NULL;
    if (replay == NULL || world == NULL) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay or world is null");
    }
    if (!fd_output_struct_valid(out, (uint32_t)sizeof(*out))) {
        return fd_diag_set(diag, FD_ERR_INVALID_SIZE, FD_FIELD_STRUCT_SIZE,
                           UINT32_C(0), "invalid replay status output");
    }
    if (world->context != replay->context) {
        return fd_diag_set(diag, FD_ERR_INVALID_ARGUMENT, FD_FIELD_NONE,
                           UINT32_C(0), "replay and world contexts differ");
    }
    remaining = replay->record_count - replay->cursor;
    count = decisions < remaining ? decisions : remaining;
    if (replay->cursor < replay->record_count) {
        expected_tick = replay->records[replay->cursor].tick_before;
        expected_hash = replay->records[replay->cursor].pre_hash;
    } else if (replay->record_count > UINT64_C(0)) {
        expected_tick = replay->records[replay->record_count - UINT64_C(1)].tick_after;
        expected_hash = replay->records[replay->record_count - UINT64_C(1)].post_hash;
    } else {
        expected_tick = UINT64_C(0);
        expected_hash = replay->tick_zero_hash;
    }
    if (world->tick != expected_tick ||
        memcmp(world->state_sha256, expected_hash, 32U) != 0) {
        return fd_diag_set(diag, FD_ERR_CHECKSUM, FD_FIELD_TICK,
                           (uint32_t)replay->cursor,
                           "world does not match replay cursor pre-state");
    }
    if (count > UINT64_C(0)) {
        fd_result clone_result = fd_world_clone_via_save(world, &candidate, diag);
        if (clone_result != FD_OK) {
            return clone_result;
        }
    }
    for (index = 0U; index < count; ++index) {
        const fd_replay_record_internal *record =
            &replay->records[replay->cursor + index];
        fd_step_result step;
        fd_result result;
        memset(&step, 0, sizeof(step));
        step.struct_size = (uint32_t)sizeof(step);
        step.abi_version = FD_ABI_VERSION;
        result = fd_world_step(candidate, &record->decision, &step, diag);
        if (result != FD_OK) {
            (void)fd_world_destroy(&candidate, NULL);
            return result;
        }
        if (memcmp(candidate->state_sha256, record->post_hash, 32U) != 0 ||
            memcmp(step.event_sha256, record->event_hash, 32U) != 0) {
            (void)fd_world_destroy(&candidate, NULL);
            return fd_diag_set(diag, FD_ERR_CHECKSUM, FD_FIELD_ACTION,
                               (uint32_t)(replay->cursor + index),
                               "replay diverged at operational boundary");
        }
    }
    if (candidate != NULL) {
        /* U01 PASS changes only tick and its canonical digest.  The verified
         * candidate makes the externally visible commit one atomic assignment. */
        world->tick = candidate->tick;
        memcpy(world->state_sha256, candidate->state_sha256, 32U);
        (void)fd_world_destroy(&candidate, NULL);
    }
    replay->cursor += count;
    memset(&status, 0, sizeof(status));
    status.struct_size = (uint32_t)sizeof(status);
    status.abi_version = FD_ABI_VERSION;
    status.cursor = replay->cursor;
    status.total_decisions = replay->record_count;
    status.decisions_advanced = count;
    status.complete = replay->cursor == replay->record_count ? UINT32_C(1) :
                                                               UINT32_C(0);
    memcpy(status.state_sha256, world->state_sha256, 32U);
    memcpy(out, &status, sizeof(status));
    return fd_diag_ok(diag);
}
