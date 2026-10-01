#include "native_q2_feedback_wire.h"
#include "qa/network_q2_messages.h"

bool application_native_q2_feedback_geometry(bool rerelease, uint8_t type,
    qa_vec3 point, qa_vec3 normal, qa_vec3 *decoded_point, qa_vec3 *decoded_normal,
    qa_error *error) {
    if (!decoded_point || !decoded_normal) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q2 feedback geometry outputs");
        return false;
    }
    qa_q2_codec codec;
    qa_net_protocol_id protocol = {.kind = rerelease ? QA_NET_Q2KEX_2023 : QA_NET_Q2_34};
    if (!qa_q2_codec_init(&codec, protocol, error)) return false;
    qa_q2_temp_entity source = {.type = type, .field_count = 2, .fields = {
        {.name = QA_Q2_TEMP_POSITION1, .kind = QA_Q2_TEMP_VECTOR,
         .value.vector = {point.x, point.y, point.z}},
        {.name = QA_Q2_TEMP_DIRECTION, .kind = QA_Q2_TEMP_VECTOR,
         .value.vector = {normal.x, normal.y, normal.z}}}};
    uint8_t bytes[16];
    qa_net_writer writer;
    qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!qa_q2_temp_entity_write(&codec, &writer, false, &source)) return false;
    qa_net_reader reader;
    qa_net_reader_init(&reader, (qa_bytes){bytes, writer.bit / 8}, error);
    qa_q2_temp_entity decoded;
    if (!qa_q2_temp_entity_read(&codec, &reader, false, &decoded) ||
        reader.bit != writer.bit || decoded.type != type || decoded.field_count != 2) return false;
    *decoded_point = qa_v3(decoded.fields[0].value.vector[0],
        decoded.fields[0].value.vector[1], decoded.fields[0].value.vector[2]);
    *decoded_normal = qa_v3(decoded.fields[1].value.vector[0],
        decoded.fields[1].value.vector[1], decoded.fields[1].value.vector[2]);
    return true;
}
