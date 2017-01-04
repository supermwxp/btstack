/*
 * Copyright (C) 2016 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "btstack.h"
#include "avdtp.h"
#include "avdtp_util.h"

inline uint8_t avdtp_header(uint8_t tr_label, avdtp_packet_type_t packet_type, avdtp_message_type_t msg_type){
    return (tr_label<<4) | ((uint8_t)packet_type<<2) | (uint8_t)msg_type;
}

int get_bit16(uint16_t bitmap, int position){
    return (bitmap >> position) & 1;
}

uint8_t store_bit16(uint16_t bitmap, int position, uint8_t value){
    if (value){
        bitmap |= 1 << position;
    } else {
        bitmap &= ~ (1 << position);
    }
    return bitmap;
}

void avdtp_read_signaling_header(avdtp_signaling_packet_t * signaling_header, uint8_t * packet, uint16_t size){
    if (size < 2) return;   
    int pos = 0;
    signaling_header->transaction_label = packet[pos] >> 4;
    signaling_header->packet_type = (avdtp_packet_type_t)((packet[pos] >> 2) & 0x03);
    signaling_header->message_type = (avdtp_message_type_t) (packet[pos] & 0x03);
    pos++;
    memset(signaling_header->command, 0, sizeof(signaling_header->command));
    switch (signaling_header->packet_type){
        case AVDTP_SINGLE_PACKET:
            signaling_header->num_packets = 0;
            signaling_header->offset = 0;
            signaling_header->size = 0;
            break;
        case AVDTP_END_PACKET:
            signaling_header->num_packets = 0;
            break;
        case AVDTP_START_PACKET:
            signaling_header->num_packets = packet[pos++];
            signaling_header->size = 0;
            signaling_header->offset = 0;
            break;
        case AVDTP_CONTINUE_PACKET:
            if (signaling_header->num_packets <= 0) {
                printf("    ERR: wrong num fragmented packets\n");
                break;
            }
            signaling_header->num_packets--;
            break;
    }
    signaling_header->signal_identifier = packet[pos] & 0x3f;
}

int avdtp_pack_service_capabilities(uint8_t * buffer, int size, avdtp_capabilities_t caps, avdtp_service_category_t category, uint8_t pack_all_capabilities){
    int i;
    // pos = 0 reserved for length
    int pos = 1;
    switch(category){
        case AVDTP_MEDIA_TRANSPORT:
        case AVDTP_REPORTING:
            break;
        case AVDTP_DELAY_REPORTING:
            if (!pack_all_capabilities) break;
            break;
        case AVDTP_RECOVERY:
            buffer[pos++] = caps.recovery.recovery_type; // 0x01=RFC2733
            buffer[pos++] = caps.recovery.maximum_recovery_window_size;
            buffer[pos++] = caps.recovery.maximum_number_media_packets;
            break;
        case AVDTP_CONTENT_PROTECTION:
            buffer[pos++] = caps.content_protection.cp_type_value_len + 2;
            big_endian_store_16(buffer, pos, caps.content_protection.cp_type);
            pos += 2;
            memcpy(buffer+pos, caps.content_protection.cp_type_value, caps.content_protection.cp_type_value_len);
            printf("AVDTP_CONTENT_PROTECTION 0%04x\n", caps.content_protection.cp_type);
            break;
        case AVDTP_HEADER_COMPRESSION:
            buffer[pos++] = (caps.header_compression.back_ch << 7) | (caps.header_compression.media << 6) | (caps.header_compression.recovery << 5);
            break;
        case AVDTP_MULTIPLEXING:
            buffer[pos++] = caps.multiplexing_mode.fragmentation << 7;
            for (i=0; i<caps.multiplexing_mode.transport_identifiers_num; i++){
                buffer[pos++] = caps.multiplexing_mode.transport_session_identifiers[i] << 7;
                buffer[pos++] = caps.multiplexing_mode.tcid[i] << 7;
                // media, reporting. recovery
            }
            break;
        case AVDTP_MEDIA_CODEC:
            buffer[pos++] = ((uint8_t)caps.media_codec.media_type) << 4;
            buffer[pos++] = (uint8_t)caps.media_codec.media_codec_type;
            for (i = 0; i<caps.media_codec.media_codec_information_len; i++){
                buffer[pos++] = caps.media_codec.media_codec_information[i];
            }
            break;
    }
    buffer[0] = pos - 1; // length
    return pos;
}

static int avdtp_unpack_service_capabilities_has_errors(avdtp_connection_t * connection, avdtp_service_category_t category, uint8_t cap_len){
    connection->error_code = 0;
        
    if (category < AVDTP_MEDIA_TRANSPORT || category > AVDTP_DELAY_REPORTING){
        printf("    ACP: BAD SERVICE CATEGORY %d\n", category);
        connection->reject_service_category = category;
        connection->error_code = BAD_SERV_CATEGORY;
        return 1;
    }
    if (connection->signaling_packet.signal_identifier == AVDTP_SI_RECONFIGURE){
        if (category != AVDTP_CONTENT_PROTECTION && category != AVDTP_MEDIA_CODEC){
            printf("    ACP: REJECT CATEGORY, INVALID_CAPABILITIES\n");
            connection->reject_service_category = category;
            connection->error_code = INVALID_CAPABILITIES;
            return 1;
        }
    }

    switch(category){
        case AVDTP_MEDIA_TRANSPORT:   
            if (cap_len != 0){
                printf("    ACP: REJECT CATEGORY, BAD_MEDIA_TRANSPORT\n");
                connection->reject_service_category = category;
                connection->error_code = BAD_MEDIA_TRANSPORT_FORMAT;
                return 1;
            }
            break;
        case AVDTP_REPORTING:                
        case AVDTP_DELAY_REPORTING:                
            if (cap_len != 0){
                printf("    ACP: REJECT CATEGORY, BAD_LENGTH\n");
                connection->reject_service_category = category;
                connection->error_code = BAD_LENGTH;
                return 1;
            }
            break;
        case AVDTP_RECOVERY:     
            if (cap_len < 3){
                printf("    ACP: REJECT CATEGORY, BAD_MEDIA_TRANSPORT\n");
                connection->reject_service_category = category;
                connection->error_code = BAD_RECOVERY_FORMAT;
                return 1;
            }           
            break;
        case AVDTP_CONTENT_PROTECTION:
            if (cap_len < 2){
                connection->reject_service_category = category;
                connection->error_code = BAD_CP_FORMAT;
                return 1;
            }
            break;
        case AVDTP_HEADER_COMPRESSION:
            break;
        case AVDTP_MULTIPLEXING:                
            break;
        case AVDTP_MEDIA_CODEC:                
            break;
        default:
            break;
    }
    return 0;
}

uint16_t avdtp_unpack_service_capabilities(avdtp_connection_t * connection, avdtp_capabilities_t * caps, uint8_t * packet, uint16_t size){
    uint16_t registered_service_categories = 0;
    int pos = 0;
    int i;
    avdtp_service_category_t category = (avdtp_service_category_t)packet[pos++];
    uint8_t cap_len = packet[pos++];
    if (avdtp_unpack_service_capabilities_has_errors(connection, category, cap_len)) return 0;
    
    int processed_cap_len = 0;
    while (pos < size){
        processed_cap_len = pos;
        switch(category){
            case AVDTP_MEDIA_TRANSPORT:   
                break;
            case AVDTP_REPORTING:                
                break;
            case AVDTP_DELAY_REPORTING:                
                break;
            case AVDTP_RECOVERY:                
                caps->recovery.recovery_type = packet[pos++];
                caps->recovery.maximum_recovery_window_size = packet[pos++];
                caps->recovery.maximum_number_media_packets = packet[pos++];
                break;
            case AVDTP_CONTENT_PROTECTION:
                caps->content_protection.cp_type = big_endian_read_16(packet, pos);
                pos+=2;
                
                caps->content_protection.cp_type_value_len = cap_len - 2;
                pos += caps->content_protection.cp_type_value_len;
                
                // connection->reject_service_category = category;
                // connection->error_code = UNSUPPORTED_CONFIGURATION;
                // support for content protection goes here
                return 0;
                
            case AVDTP_HEADER_COMPRESSION:
                caps->header_compression.back_ch  = packet[pos] >> 7; 
                caps->header_compression.media    = packet[pos] >> 6;
                caps->header_compression.recovery = packet[pos] >> 5;
                pos++;
                break;
            case AVDTP_MULTIPLEXING:                
                caps->multiplexing_mode.fragmentation = packet[pos++] >> 7;
                // read [tsid, tcid] for media, reporting. recovery respectively
                caps->multiplexing_mode.transport_identifiers_num = 3;
                for (i=0; i<caps->multiplexing_mode.transport_identifiers_num; i++){
                    caps->multiplexing_mode.transport_session_identifiers[i] = packet[pos++] >> 7;
                    caps->multiplexing_mode.tcid[i] = packet[pos++] >> 7;
                }
                break;
            case AVDTP_MEDIA_CODEC:                
                caps->media_codec.media_type = packet[pos++] >> 4;
                caps->media_codec.media_codec_type = packet[pos++];
                caps->media_codec.media_codec_information_len = cap_len - 2;
                caps->media_codec.media_codec_information = &packet[pos];
                pos += caps->media_codec.media_codec_information_len;
                break;
            default:
                break;
        }

        registered_service_categories = store_bit16(registered_service_categories, category, 1);
        processed_cap_len = pos - processed_cap_len;
        if (cap_len <= processed_cap_len && pos < size-2){
            category = (avdtp_service_category_t)packet[pos++];
            cap_len = packet[pos++];
            if (avdtp_unpack_service_capabilities_has_errors(connection, category, cap_len)) return 0;
        }
    }
    return registered_service_categories;
}

void avdtp_prepare_capabilities(avdtp_signaling_packet_t * signaling_packet, uint8_t transaction_label, uint16_t registered_service_categories, avdtp_capabilities_t capabilities, uint8_t identifier){
    if (signaling_packet->offset) return;
    uint8_t pack_all_capabilities = 1;
    signaling_packet->size = 0;
    int i;
    switch (identifier) {
        case AVDTP_SI_GET_CAPABILITIES:
            pack_all_capabilities = 0;
            break;
        case AVDTP_SI_SET_CONFIGURATION:
            signaling_packet->command[signaling_packet->size++] = signaling_packet->acp_seid << 2;
            signaling_packet->command[signaling_packet->size++] = signaling_packet->int_seid << 2;
            break;
        default: 
            break;
    } 
    
    for (i = 1; i < 9; i++){
        if (get_bit16(registered_service_categories, i)){
            // service category
            signaling_packet->command[signaling_packet->size++] = i;
            signaling_packet->size += avdtp_pack_service_capabilities(signaling_packet->command+signaling_packet->size, sizeof(signaling_packet->command)-signaling_packet->size, capabilities, (avdtp_service_category_t)i, pack_all_capabilities);
        }
    }
    // signaling_packet->command[signaling_packet->size++] = 0x04;
    // signaling_packet->command[signaling_packet->size++] = 0x02;
    // signaling_packet->command[signaling_packet->size++] = 0x02;
    // signaling_packet->command[signaling_packet->size++] = 0x00;
    
    signaling_packet->signal_identifier = identifier;
    signaling_packet->transaction_label = transaction_label;
    if (identifier == AVDTP_SI_SET_CONFIGURATION){
        signaling_packet->message_type = AVDTP_CMD_MSG;
    } else {
        signaling_packet->message_type = AVDTP_RESPONSE_ACCEPT_MSG;
    }
}

int avdtp_signaling_create_fragment(uint16_t cid, avdtp_signaling_packet_t * signaling_packet, uint8_t * out_buffer) {
    int mtu = l2cap_get_remote_mtu_for_local_cid(cid);
    // hack for test
    // int mtu = 6;
    int data_len = 0;

    uint16_t offset = signaling_packet->offset;
    uint16_t pos = 1;
    // printf(" avdtp_signaling_create_fragment offset %d, packet type %d\n",  signaling_packet->offset, signaling_packet->packet_type);
    
    if (offset == 0){
        if (signaling_packet->size <= mtu - 2){
            // printf(" AVDTP_SINGLE_PACKET\n");
            signaling_packet->packet_type = AVDTP_SINGLE_PACKET;
            out_buffer[pos++] = signaling_packet->signal_identifier;
            data_len = signaling_packet->size;
        } else {
            signaling_packet->packet_type = AVDTP_START_PACKET;
            out_buffer[pos++] = (mtu + signaling_packet->size)/ (mtu-1);
            out_buffer[pos++] = signaling_packet->signal_identifier;
            data_len = mtu - 3;
            signaling_packet->offset = data_len;
            // printf(" AVDTP_START_PACKET len %d, offset %d\n", signaling_packet->size, signaling_packet->offset);
        }
    } else {
        int remaining_bytes = signaling_packet->size - offset;
        if (remaining_bytes <= mtu - 1){
            //signaling_packet->fragmentation = 1;
            signaling_packet->packet_type = AVDTP_END_PACKET;
            data_len = remaining_bytes;
            signaling_packet->offset = 0;
            // printf(" AVDTP_END_PACKET len %d, offset %d\n", signaling_packet->size, signaling_packet->offset);
        } else{
            signaling_packet->packet_type = AVDTP_CONTINUE_PACKET;
            data_len = mtu - 1;
            signaling_packet->offset += data_len;
            // printf(" AVDTP_CONTINUE_PACKET len %d, offset %d\n", signaling_packet->size, signaling_packet->offset);
        }
    }
    out_buffer[0] = avdtp_header(signaling_packet->transaction_label, signaling_packet->packet_type, signaling_packet->message_type);
    memcpy(out_buffer+pos, signaling_packet->command + offset, data_len);
    pos += data_len; 
    return pos;
}
