#include "internal.h"

#include "qa/network_q2_messages.h"

#include <math.h>

void native_host_message_clear(qa_native_host *host)
{
    if (!host)
        return;
    host->message_size = 0;
    host->message_failed = false;
}

static bool append(qa_native_host *host, const void *data, size_t size, qa_error *error)
{
    if (host->message_failed)
        return native_host_fail(error, QA_ERROR_FORMAT, host->message_size,
                                "native Q2 message is already overflowed");
    if (size > host->message_capacity - host->message_size) {
        host->message_failed = true;
        return native_host_fail(error, QA_ERROR_FORMAT, host->message_size,
                                "native Q2 message exceeds its configured capacity");
    }
    if (size)
        memcpy(host->message + host->message_size, data, size);
    host->message_size += size;
    return true;
}

static bool append_u8(qa_native_host *host, uint8_t value, qa_error *error)
{
    return append(host, &value, 1, error);
}

static bool append_u16(qa_native_host *host, uint16_t value, qa_error *error)
{
    uint8_t bytes[2];
    qa_store_u16le(bytes, value);
    return append(host, bytes, sizeof(bytes), error);
}

static bool append_u32(qa_native_host *host, uint32_t value, qa_error *error)
{
    uint8_t bytes[4];
    qa_store_u32le(bytes, value);
    return append(host, bytes, sizeof(bytes), error);
}

static bool append_f32(qa_native_host *host, float value, qa_error *error)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return append_u32(host, bits, error);
}

static bool write_direction(qa_native_host *host, qa_native_address address, qa_error *error)
{
    if (!address)
        return append_u8(host, 0, error);
    qa_vec3 direction;
    if (!native_host_read_vec3(host, address, &direction, error))
        return false;
    uint8_t byte = 0;
    qa_net_writer writer;
    qa_net_writer_init(&writer, &byte, sizeof(byte), error);
    float vector[] = {direction.x, direction.y, direction.z};
    if (!qa_q2_write_dir(&writer, vector)) {
        host->message_failed = true;
        return false;
    }
    return append_u8(host, byte, error);
}

bool native_host_message_write(qa_native_host *host, const qa_native_import_call *call,
                               qa_error *error)
{
    if (!host || !call)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native message writer call is required");
    if (!strcmp(call->name, "WriteChar"))
        return append_u8(host, (uint8_t)(int8_t)native_argument_i32(call, 0), error);
    if (!strcmp(call->name, "WriteByte"))
        return append_u8(host, (uint8_t)native_argument_i32(call, 0), error);
    if (!strcmp(call->name, "WriteShort"))
        return append_u16(host, (uint16_t)(int16_t)native_argument_i32(call, 0), error);
    if (!strcmp(call->name, "WriteLong"))
        return append_u32(host, (uint32_t)native_argument_i32(call, 0), error);
    if (!strcmp(call->name, "WriteFloat"))
        return append_f32(host, native_argument_f32(call, 0), error);
    if (!strcmp(call->name, "WriteAngle")) {
        float angle = native_argument_f32(call, 0);
        if (!isfinite(angle))
            return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                    "native Q2 message angle is not finite");
        int32_t encoded = (int32_t)truncf(angle * (256.0f / 360.0f));
        return append_u8(host, (uint8_t)encoded, error);
    }
    if (!strcmp(call->name, "WriteString")) {
        qa_native_address address = native_argument_address(call, 0);
        if (!address)
            return append_u8(host, 0, error);
        qa_buffer string = {0};
        if (!native_host_string_read(host, address, &string, error))
            return false;
        bool ok = append(host, string.data, string.size, error) && append_u8(host, 0, error);
        qa_buffer_free(&string);
        return ok;
    }
    if (!strcmp(call->name, "WritePosition")) {
        qa_vec3 position;
        if (!native_host_read_vec3(host, native_argument_address(call, 0), &position, error))
            return false;
        if (host->profile == QA_NATIVE_Q2_GAME_API2023)
            return append_f32(host, position.x, error) && append_f32(host, position.y, error) &&
                   append_f32(host, position.z, error);
        float values[] = {position.x, position.y, position.z};
        for (size_t index = 0; index < 3; ++index) {
            double scaled = trunc((double)values[index] * 8.0);
            if (!isfinite(scaled) || scaled < INT16_MIN || scaled > INT16_MAX)
                return native_host_fail(error, QA_ERROR_ARGUMENT, index,
                                        "API 3 message position exceeds int16 coordinates");
            if (!append_u16(host, (uint16_t)(int16_t)scaled, error))
                return false;
        }
        return true;
    }
    if (!strcmp(call->name, "WriteDir"))
        return write_direction(host, native_argument_address(call, 0), error);
    if (!strcmp(call->name, "WriteEntity")) {
        uint32_t slot;
        qa_actor_id actor;
        if (!native_host_actor_for_address(host, native_argument_address(call, 0), false,
                                           &actor, &slot, error))
            return false;
        if (!actor.registry || slot > INT16_MAX)
            return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                                    "native Q2 message entity is not representable");
        return append_u16(host, (uint16_t)slot, error);
    }
    return native_host_fail(error, QA_ERROR_NOT_FOUND, call->slot,
                            "unknown native Q2 message writer");
}

bool native_host_message_send(qa_native_host *host, const qa_native_import_call *call,
                              qa_error *error)
{
    if (!host || !call || (!strcmp(call->name, "multicast") ==
                           !strcmp(call->name, "unicast")))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native Q2 message delivery call is invalid");
    if (host->message_failed) {
        native_host_message_clear(host);
        return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                "native Q2 message overflowed before delivery");
    }
    qa_native_host_message message = {
        .payload = {host->message, host->message_size},
        .target = !strcmp(call->name, "unicast") ? QA_NATIVE_HOST_UNICAST
                                                 : QA_NATIVE_HOST_MULTICAST};
    bool deliver = host->message_size != 0;
    if (message.target == QA_NATIVE_HOST_UNICAST) {
        qa_native_address address = native_argument_address(call, 0);
        if (!address) {
            native_host_message_clear(host);
            return true;
        }
        uint32_t slot;
        if (!native_host_actor_for_address(host, address, false, &message.client, &slot,
                                           error)) {
            native_host_message_clear(host);
            return false;
        }
        deliver = deliver && message.client.registry != 0;
        message.reliable = host->profile == QA_NATIVE_Q2_GAME_API3
                               ? native_argument_i32(call, 1) != 0
                               : call->arguments[1].as.u8 != 0;
        if (host->profile == QA_NATIVE_Q2_GAME_API2023)
            message.flags = native_argument_u32(call, 2);
    } else {
        qa_native_address origin = native_argument_address(call, 0);
        if (origin && !native_host_read_vec3(host, origin, &message.origin, error)) {
            native_host_message_clear(host);
            return false;
        }
        message.positioned = origin != 0;
        message.destination = native_argument_i32(call, 1);
        if (host->profile == QA_NATIVE_Q2_GAME_API2023)
            message.reliable = call->arguments[2].as.u8 != 0;
        else if (message.destination >= 3) {
            message.destination -= 3;
            message.reliable = true;
        }
    }
    bool ok = !deliver || !host->engine.message ||
              host->engine.message(host->engine.context, &message, error);
    if (deliver && !host->engine.message)
        ok = native_host_fail(error, QA_ERROR_UNSUPPORTED, call->slot,
                              "native Q2 message delivery is unbound");
    native_host_message_clear(host);
    return ok;
}
