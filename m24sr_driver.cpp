/**
 ******************************************************************************
 * @file    m24sr_driver.cpp
 * @author  ST Central Labs
 * @brief   This file provides a set of functions to interface with the M24SR
 *          device.
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT(c) 2018 STMicroelectronics</center></h2>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *   3. Neither the name of STMicroelectronics nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************
 */

#include <m24sr_driver.h>

namespace nfc {
namespace vendor {
namespace ST {

#define MAX_OPERATION_SIZE         246
#define MAX_PAYLOAD                241

/** value returned by the NFC chip when a command is successfully completed */
#define NFC_COMMAND_SUCCESS        0x9000
/** I2C nfc address */
#define M24SR_ADDR                 0xAC

#define SYSTEM_FILE_ID_BYTES      {0xE1,0x01}
#define CC_FILE_ID_BYTES          {0xE1,0x03}

#define UB_STATUS_OFFSET           4
#define LB_STATUS_OFFSET           3

/* APDU command: class list */
#define C_APDU_CLA_DEFAULT         0x00
#define C_APDU_CLA_ST              0xA2

/* data area management commands */
#define C_APDU_SELECT_FILE         0xA4
#define C_APDU_GET_RESPONCE        0xC0
#define C_APDU_STATUS              0xF2
#define C_APDU_UPDATE_BINARY       0xD6
#define C_APDU_READ_BINARY         0xB0
#define C_APDU_WRITE_BINARY        0xD0
#define C_APDU_UPDATE_RECORD       0xDC
#define C_APDU_READ_RECORD         0xB2

/* safety management commands */
#define C_APDU_VERIFY              0x20
#define C_APDU_CHANGE              0x24
#define C_APDU_DISABLE             0x26
#define C_APDU_ENABLE              0x28

/* GPO management commands */
#define C_APDU_INTERRUPT           0xD6

/* length */
#define STATUS_NBBYTE                 2
#define CRC_NBBYTE                    2
#define STATUSRESPONSE_NBBYTE         5
#define DESELECTREQUEST_COMMAND      {0xC2,0xE0,0xB4}
#define DESELECTRESPONSE_NBBYTE       3
#define WATINGTIMEEXTRESPONSE_NBBYTE  4
#define PASSWORD_NBBYTE               0x10
#define SELECTAPPLICATION_COMMAND    {0xD2,0x76,0x00,0x00,0x85,0x01,0x01}

/* command structure mask */
#define CMD_MASK_SELECTAPPLICATION    0x01FF
#define CMD_MASK_SELECTCCFILE         0x017F
#define CMD_MASK_SELECTNDEFFILE       0x017F
#define CMD_MASK_READBINARY           0x019F
#define CMD_MASK_UPDATEBINARY         0x017F
#define CMD_MASK_VERIFYBINARYWOPWD    0x013F
#define CMD_MASK_VERIFYBINARYWITHPWD  0x017F
#define CMD_MASK_CHANGEREFDATA        0x017F
#define CMD_MASK_ENABLEVERIFREQ       0x011F
#define CMD_MASK_DISABLEVERIFREQ      0x011F
#define CMD_MASK_SENDINTERRUPT        0x013F
#define CMD_MASK_GPOSTATE             0x017F

/* command structure values for the mask */
#define PCB_NEEDED                0x0001      /* PCB byte present or not */
#define CLA_NEEDED                0x0002      /* CLA byte present or not */
#define INS_NEEDED                0x0004      /* Operation code present or not*/
#define P1_NEEDED                 0x0008      /* Selection Mode  present or not*/
#define P2_NEEDED                 0x0010      /* Selection Option present or not*/
#define LC_NEEDED                 0x0020      /* Data field length byte present or not */
#define DATA_NEEDED               0x0040      /* Data present or not */
#define LE_NEEDED                 0x0080      /* Expected length present or not */
#define CRC_NEEDED                0x0100      /* 2 CRC bytes present  or not */
#define DID_NEEDED                0x08        /* DID byte present or not */

/*  offset */
#define OFFSET_PCB                0
#define OFFSET_CLASS              1
#define OFFSET_INS                2
#define OFFSET_P1                 3

/*  mask */
#define MASK_BLOCK                0xC0
#define MASK_IBLOCK               0x00
#define MASK_RBLOCK               0x80
#define MASK_SBLOCK               0xC0

#define GETMSB(val)               ((uint8_t) ((val & 0xFF00)>>8))
#define GETLSB(val)               ((uint8_t) (val & 0x00FF))

/** default password, also used to enable super user mode through the I2C channel */
const uint8_t M24srDriver::default_password[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/**
 * @brief This function updates the CRC
 */
static uint16_t update_crc(uint8_t ch, uint16_t *lpw_crc) {
    ch = (ch ^ (uint8_t) ((*lpw_crc) & 0x00FF));
    ch = (ch ^ (ch << 4));
    *lpw_crc = (*lpw_crc >> 8) ^ ((uint16_t) ch << 8) ^ ((uint16_t) ch << 3) ^ ((uint16_t) ch >> 4);

    return (*lpw_crc);
}

/**
 * @brief This function returns the CRC 16
 * @param data  pointer on the data used to compute the CRC16
 * @param length  number of bytes of the data
 * @retval CRC16 
 */
static uint16_t compute_crc(uint8_t *data, uint8_t length) {
    uint8_t chBlock;
    uint16_t crc16 = 0x6363; // ITU-V.41

    do {
        chBlock = *data++;
        update_crc(chBlock, &crc16);
    } while (--length);

    return crc16;
}

/**  
 * @brief This function computes the CRC16 residue as defined by CRC ISO/IEC 13239
 * @param DataIn         input data
 * @param Length         Number of bits of DataIn
 * @retval Status (SW1&SW2)       CRC16 residue is correct
 * @retval M24SR_ERROR_CRC       CRC16 residue is false
 */
static M24srError_t is_correct_crc_residue(uint8_t *data, uint8_t length) {
    uint16_t res_crc = 0x0000;
    M24srError_t status;
    /* check the CRC16 Residue */
    if (length != 0) {
        res_crc = compute_crc(data, length);
    }

    if (res_crc == 0x0000) {
        /* Good CRC, but error status from M24SR */
        status = (M24srError_t) (((data[length - UB_STATUS_OFFSET] << 8) & 0xFF00)
            | (data[length - LB_STATUS_OFFSET] & 0x00FF));
    } else {
        res_crc = 0x0000;
        res_crc = compute_crc(data, 5);
        if (res_crc != 0x0000) {
            /* Bad CRC */
            return M24SR_IO_ERROR_CRC;
        } else {
            /* Good CRC, but error status from M24SR */
            status = (M24srError_t) (((data[1] << 8) & 0xFF00) | (data[2] & 0x00FF));
        }
    }
    if (status == NFC_COMMAND_SUCCESS) {
        status = M24SR_SUCCESS;
    }

    return status;
}

/**
 * @brief This functions creates an I block command according to the structures command_mask and Command.
 * @param command_mask  structure which contains the field of the different parameters
 * @param command  structure of the command
 * @param length  number of bytes of the command
 * @param command_buffer  pointer to the command created
 */
static void build_I_block_command(uint16_t command_mask, C_APDU *command, uint8_t did, uint16_t *length,
                                  uint8_t *command_buffer) {
    uint16_t crc16;
    static uint8_t block_number = 0x01;

    (*length) = 0;

    /* add the PCD byte */
    if ((command_mask & PCB_NEEDED) != 0) {
        /* toggle the block number */
        block_number = !block_number;
        /* Add the I block byte */
        command_buffer[(*length)++] = 0x02 | block_number;
    }

    /* add the DID byte */
    if ((block_number & DID_NEEDED) != 0) {
        /* Add the I block byte */
        command_buffer[(*length)++] = did;
    }

    /* add the Class byte */
    if ((command_mask & CLA_NEEDED) != 0) {
        command_buffer[(*length)++] = command->header.CLA;
    }

    /* add the instruction byte byte */
    if ((command_mask & INS_NEEDED) != 0) {
        command_buffer[(*length)++] = command->header.INS;
    }

    /* add the Selection Mode byte */
    if ((command_mask & P1_NEEDED) != 0) {
        command_buffer[(*length)++] = command->header.P1;
    }

    /* add the Selection Mode byte */
    if ((command_mask & P2_NEEDED) != 0) {
        command_buffer[(*length)++] = command->header.P2;
    }

    /* add Data field lengthbyte */
    if ((command_mask & LC_NEEDED) != 0) {
        command_buffer[(*length)++] = command->body.LC;
    }

    /* add Data field  */
    if ((command_mask & DATA_NEEDED) != 0) {
        if (command->body.data) {
            memcpy(&(command_buffer[(*length)]), command->body.data, command->body.LC);
        } else {
            memset(&(command_buffer[(*length)]), 0, command->body.LC);
        }
        (*length) += command->body.LC;
    }

    /* add Le field  */
    if ((command_mask & LE_NEEDED) != 0) {
        command_buffer[(*length)++] = command->body.LE;
    }

    /* add CRC field  */
    if ((command_mask & CRC_NEEDED) != 0) {
        crc16 = compute_crc(command_buffer, (uint8_t) (*length));
        /* append the CRC16 */
        command_buffer[(*length)++] = GETLSB(crc16);
        command_buffer[(*length)++] = GETMSB(crc16);
    }
}

/**  
 * @brief This function returns M24SR_STATUS_SUCCESS if the buffer is an s-block
 * @param buffer        pointer to the data
 * @retval M24SR_SUCCESS  the data is a S-Block
 * @retval NFC_ERROR      the data is not a S-Block
 */
static M24srError_t is_S_block(uint8_t *buffer) {
    if ((buffer[OFFSET_PCB] & MASK_BLOCK) == MASK_SBLOCK) {
        return M24SR_SUCCESS;
    } else {
        return M24SR_ERROR;
    }
}

/**
 * @brief This function sends the FWT extension command (S-Block format)
 * @param fwt_byte  FWT value
 * @return M24SR_SUCCESS if no errors
 */
M24srError_t M24srDriver::send_fwt_extension(uint8_t fwt_byte) {
    uint8_t buffer[STATUSRESPONSE_NBBYTE];
    M24srError_t status;
    uint8_t length = 0;
    uint16_t crc16;

    /* create the response */
    buffer[length++] = 0xF2;
    buffer[length++] = fwt_byte;
    /* compute the CRC */
    crc16 = compute_crc(buffer, 0x02);
    /* append the CRC16 */
    buffer[length++] = GETLSB(crc16);
    buffer[length++] = GETMSB(crc16);

    /* send the request */
    status = io_send_i2c_command(length, buffer);
    if (status != M24SR_SUCCESS) {
        return status;
    }

    _last_command = UPDATE;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_update_binary();
        } else {
            _last_command = NONE;
            get_callback()->on_updated_binary(this, status, _last_command_data.offset, _last_command_data.data,
                                              _last_command_data.length);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srDriver::M24srDriver()
    : _i2c_channel(NFC_I2C_SDA_PIN, NFC_I2C_SCL_PIN),
      _gpo_event_interrupt(NFC_GPO_PIN),
      _gpo_pin(NFC_GPO_PIN),
      _rf_disable_pin(NFC_RF_DISABLE_PIN),
      _callback(&_default_cb),
      _component_cb(NULL),
      _communication_type(SYNC),
      _last_command(NONE),
      _ndef_size(MAX_NDEF_SIZE),
      _max_read_bytes(MAX_PAYLOAD),
      _max_write_bytes(MAX_PAYLOAD),
      _is_session_open(false) {
    memset(_buffer, 0, 0xFF);
    _did_byte = 0;
    if (_rf_disable_pin.is_connected() != 0) {
        _rf_disable_pin = 0;
    }
    if (_gpo_pin.is_connected() != 0) {
        _gpo_event_interrupt.fall(&nfc_interrupt_callback);
        _gpo_event_interrupt.mode(PullUp);
        _gpo_event_interrupt.disable_irq();
    }
}

/**
 * @brief This function initialize the M24SR device
 * @return M24SR_SUCCESS if no errors
 */
M24srError_t M24srDriver::init() {
    //force to open a i2c session
    M24srError_t status = get_session(true);

    if (status != M24SR_SUCCESS) {
        return status;
    }

    //leave the gpo always up
    if (_gpo_pin.is_connected() != 0) {
        status = manage_i2c_gpo(HIGH_IMPEDANCE);
        if (status != M24SR_SUCCESS)
            return status;
    }

    if (_rf_disable_pin.is_connected() != 0) {
        status = manage_rf_gpo(HIGH_IMPEDANCE);
        if (status != M24SR_SUCCESS)
            return status;
    }

    //close the session
    status = deselect();

    if (status != M24SR_SUCCESS) {
        return status;
    }

    if (_gpo_pin.is_connected() != 0) {
        _gpo_event_interrupt.enable_irq();
    }

    return M24SR_SUCCESS;
}

/**
 * @brief This function sends the Deselect command (S-Block format)
 * @return M24SR_SUCCESS if no errors
 */
M24srError_t M24srDriver::deselect() {
    uint8_t buffer[] = DESELECTREQUEST_COMMAND;
    M24srError_t status;

    /* send the request */
    status = io_send_i2c_command(sizeof(buffer), buffer);

    if (status != M24SR_SUCCESS) {
        get_callback()->on_deselect(this, status);
    }

    _last_command = DESELECT;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_deselect();
        } else {
            _last_command = NONE;
            get_callback()->on_selected_application(this, status);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_deselect() {
    uint8_t buffer[4];
    M24srError_t status;

    status = io_receive_i2c_response(sizeof(buffer), buffer);
    get_callback()->on_deselect(this, status);

    return status;
}

/**
 * @brief This function sends the GetSession command to the M24SR device
 * @retval M24SR_SUCCESS the function is successful.
 * @retval Status (SW1&SW2) if operation does not complete.
 */
M24srError_t M24srDriver::get_session(bool force) {
    /* special M24SR command */
    const uint8_t M24SR_OPENSESSION_COMMAND = 0x26;
    const uint8_t M24SR_KILLSESSION_COMMAND = 0x52;

    M24srError_t status;

    if (force) {
        status = io_send_i2c_command(1, &M24SR_OPENSESSION_COMMAND);
    } else {
        status = io_send_i2c_command(1, &M24SR_KILLSESSION_COMMAND);
    }

    if (status != M24SR_SUCCESS) {
        get_callback()->on_session_open(this, status);
        return status;
    }

    /* Insure no access will be done just after open session */
    /* The only way here is to poll I2C to know when M24SR is ready */
    /* GPO can not be use with KillSession command */
    status = io_poll_i2c();

    get_callback()->on_session_open(this, status);
    return status;
}

/**
 * @brief This function sends the SelectApplication command
 * @return M24SR_SUCCESS if no errors
 */
M24srError_t M24srDriver::select_application() {
    C_APDU command;
    M24srError_t status;
    uint8_t data_out[] = SELECTAPPLICATION_COMMAND;
    uint16_t P1_P2 =0x0400;
    uint16_t length;

    /* build the command */
    command.header.CLA = C_APDU_CLA_DEFAULT;
    command.header.INS = C_APDU_SELECT_FILE;
    /* copy the offset */
    command.header.P1 = GETMSB(P1_P2);
    command.header.P2 = GETLSB(P1_P2);
    /* copy the number of byte of the data field */
    command.body.LC = sizeof(data_out);
    /* copy the data */
    command.body.data = data_out;
    /* copy the number of byte to read */
    command.body.LE = 0;
    /* build the I2C command */
    build_I_block_command(CMD_MASK_SELECTAPPLICATION, &command, _did_byte, &length, _buffer);

    /* send the request */
    status = io_send_i2c_command(length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_selected_application(this, status);
        return status;
    }

    _last_command = SELECT_APPLICATION;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_select_application();
        } else {
            _last_command = NONE;
            get_callback()->on_selected_application(this, status);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_select_application() {
    uint8_t data_in[STATUSRESPONSE_NBBYTE];
    M24srError_t status;

    _last_command = NONE;

    status = io_receive_i2c_response(sizeof(data_in), data_in);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_selected_application(this, status);
        return status;
    }
    status = is_correct_crc_residue(data_in, sizeof(data_in));
    get_callback()->on_selected_application(this, status);

    return status;
}

M24srError_t M24srDriver::read_id(uint8_t *nfc_id) {
    if (!nfc_id) {
        return M24SR_ERROR;
    }

    //enable the callback for change the gpo
    _component_cb = &_read_id_cb;
    _read_id_cb.set_task(nfc_id);

    //start the readID procedure
    return select_application();
}

/**
 * @brief This function sends the SelectCCFile command
 * @retval M24SR_SUCCESS the function is successful.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 * @retval Status (SW1&SW2)   if operation does not complete for another reason.
 */
M24srError_t M24srDriver::select_cc_file() {
    C_APDU command;
    M24srError_t status;
    uint8_t data_out[] = CC_FILE_ID_BYTES;
    uint16_t P1_P2 =0x000C;
    uint16_t length;

    /* build the command */
    command.header.CLA = C_APDU_CLA_DEFAULT;
    command.header.INS = C_APDU_SELECT_FILE;
    /* copy the offset */
    command.header.P1 = GETMSB(P1_P2);
    command.header.P2 = GETLSB(P1_P2);
    /* copy the number of byte of the data field */
    command.body.LC = sizeof(data_out);
    command.body.data = data_out;
    /* build the I2C command */
    build_I_block_command(CMD_MASK_SELECTCCFILE, &command, _did_byte, &length, _buffer);

    /* send the request */
    status = io_send_i2c_command(length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_selected_cc_file(this, status);
        return status;
    }

    _last_command = SELECT_CC_FILE;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_select_cc_file();
        } else {
            _last_command = NONE;
            get_callback()->on_selected_cc_file(this, status);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_select_cc_file() {
    uint8_t data_in[STATUSRESPONSE_NBBYTE];
    M24srError_t status;

    _last_command = NONE;

    status = io_receive_i2c_response(sizeof(data_in), data_in);

    if (status != M24SR_SUCCESS) {
        get_callback()->on_selected_cc_file(this, status);
        return status;
    }

    status = is_correct_crc_residue(data_in, sizeof(data_in));
    get_callback()->on_selected_cc_file(this, status);

    return status;
}

/**
 * @brief This function sends the SelectSystemFile command
 * @retval Status (SW1&SW2) Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::select_system_file() {
    C_APDU command;
    uint8_t data_out[] = SYSTEM_FILE_ID_BYTES;
    M24srError_t status;
    uint16_t P1_P2 = 0x000C;
    uint16_t length;

    /* build the command */
    command.header.CLA = C_APDU_CLA_DEFAULT;
    command.header.INS = C_APDU_SELECT_FILE;
    /* copy the offset */
    command.header.P1 = GETMSB(P1_P2);
    command.header.P2 = GETLSB(P1_P2);
    /* copy the number of byte of the data field */
    command.body.LC = sizeof(data_out);
    command.body.data = data_out;
    /* build the command */
    build_I_block_command(CMD_MASK_SELECTCCFILE, &command, _did_byte, &length, _buffer);

    /* send the request */
    status = io_send_i2c_command(length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_selected_system_file(this, status);
        return status;
    }

    _last_command = SELECT_SYSTEM_FILE;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_select_system_file();
        } else {
            _last_command = NONE;
            get_callback()->on_selected_system_file(this, status);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_select_system_file() {
    uint8_t data_in[STATUSRESPONSE_NBBYTE];
    M24srError_t status;

    _last_command = NONE;

    status = io_receive_i2c_response(sizeof(data_in), data_in);

    if (status != M24SR_SUCCESS) {
        get_callback()->on_selected_system_file(this, status);
        return status;
    }

    status = is_correct_crc_residue(data_in, sizeof(data_in));
    get_callback()->on_selected_system_file(this, status);

    return status;
}

/**
 * @brief This function sends the SelectNDEFfile command
 * @retval Status (SW1&SW2) Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::select_ndef_file(uint16_t ndef_file_id) {
    C_APDU command;
    M24srError_t status;
    uint8_t data_out[] = { GETMSB(ndef_file_id), GETLSB(ndef_file_id) };
    uint16_t P1_P2 = 0x000C;
    uint16_t length;

    /* build the command */
    command.header.CLA = C_APDU_CLA_DEFAULT;
    command.header.INS = C_APDU_SELECT_FILE;
    /* copy the offset */
    command.header.P1 = GETMSB(P1_P2);
    command.header.P2 = GETLSB(P1_P2);
    /* copy the number of byte of the data field */
    command.body.LC = sizeof(data_out);
    command.body.data = data_out;
    /* copy the offset */
    /* build the I2C command */
    build_I_block_command(CMD_MASK_SELECTNDEFFILE, &command, _did_byte, &length, _buffer);

    /* send the request */
    status = io_send_i2c_command(length, _buffer);
    if (status != M24SR_SUCCESS) {
        return status;
    }
    _last_command = SELECT_NDEF_FILE;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_select_ndef_file();
        } else {
            _last_command = NONE;
            get_callback()->on_selected_ndef_file(this, status);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_select_ndef_file() {
    uint8_t data_in[STATUSRESPONSE_NBBYTE];
    M24srError_t status;

    _last_command = NONE;

    status = io_receive_i2c_response(sizeof(data_in), data_in);

    if (status != M24SR_SUCCESS) {
        get_callback()->on_selected_ndef_file(this, status);
        return status;
    }

    status = is_correct_crc_residue(data_in, sizeof(data_in));
    get_callback()->on_selected_ndef_file(this, status);

    return status;
}

/**
 * @brief This function sends a read binary command
 * @param offset   first byte to read
 * @param length   number of bytes to read
 * @param buffer   pointer to the buffer read from the M24SR device
 * @retval Status (SW1&SW2) Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::read_binary(uint16_t offset, uint8_t length, uint8_t *buffer) {
    C_APDU command;
    uint16_t command_length;
    M24srError_t status;

    //clamp the buffer to the max size
    if (length > MAX_OPERATION_SIZE) {
        length = MAX_OPERATION_SIZE;
    }

    /* build the command */
    command.header.CLA = C_APDU_CLA_DEFAULT;
    command.header.INS = C_APDU_READ_BINARY;
    /* copy the offset */
    command.header.P1 = GETMSB(offset);
    command.header.P2 = GETLSB(offset);
    /* copy the number of byte to read */
    command.body.LE = length;

    build_I_block_command(CMD_MASK_READBINARY, &command, _did_byte, &command_length, _buffer);

    status = io_send_i2c_command(command_length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_read_byte(this, status, offset, buffer, length);
        return status;
    }

    _last_command = READ;
    _last_command_data.data = buffer;
    _last_command_data.length = length;
    _last_command_data.offset = offset;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_read_binary();
        } else {
            _last_command = NONE;
            get_callback()->on_read_byte(this, status, offset, buffer, length);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_read_binary() {
    M24srError_t status;
    const uint16_t length = _last_command_data.length;
    const uint16_t offset = _last_command_data.offset;
    uint8_t *data = _last_command_data.data;

    _last_command = NONE;

    status = io_receive_i2c_response(length + STATUSRESPONSE_NBBYTE, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_read_byte(this, status, offset, data, length);
        return status;
    }
    status = is_correct_crc_residue(_buffer, length + STATUSRESPONSE_NBBYTE);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_read_byte(this, status, offset, data, length);
    } else {
        /* retrieve the data without SW1 & SW2 as provided as return value of the function */
        memcpy(_last_command_data.data, &_buffer[1], length);
        get_callback()->on_read_byte(this, status, offset, data, length);
    }

    return status;
}

/**
 * @brief This function sends a ST read binary command (no error if access is not inside NDEF file)
 * @param offset   first byte to read
 * @param length number of bytes to read
 * @param buffer  pointer to the buffer read from the M24SR device
 * @retval Status (SW1&SW2) Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::st_read_binary(uint16_t offset, uint8_t length, uint8_t *buffer) {
    C_APDU command;
    uint16_t command_length;
    M24srError_t status;

    //clamp the buffer to the max size
    if (length > MAX_OPERATION_SIZE) {
        length = MAX_OPERATION_SIZE;
    }

    /* build the command */
    command.header.CLA = C_APDU_CLA_ST;
    command.header.INS = C_APDU_READ_BINARY;
    /* copy the offset */
    command.header.P1 = GETMSB(offset);
    command.header.P2 = GETLSB(offset);
    /* copy the number of byte to read */
    command.body.LE = length;

    build_I_block_command(CMD_MASK_READBINARY, &command, _did_byte, &command_length, _buffer);

    status = io_send_i2c_command(command_length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_read_byte(this, status, offset, buffer, length);
        return status;
    }

    _last_command = READ;
    _last_command_data.data = buffer;
    _last_command_data.length = length;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_read_binary();
        } else {
            _last_command = NONE;
            get_callback()->on_read_byte(this, status, offset, buffer, length);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

/**
 * @brief This function sends a Update binary command
 * @param offset   first byte to read
 * @param length   number of bytes to write
 * @param buffer   pointer to the buffer read from the M24SR device
 * @retval Status (SW1&SW2) Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::update_binary(uint16_t offset, uint8_t length, const uint8_t *data) {
    C_APDU command;
    M24srError_t status;
    uint16_t command_length;

    //clamp the buffer to the max size
    if (length > MAX_OPERATION_SIZE) {
        length = MAX_OPERATION_SIZE;
    }

    /* build the command */
    command.header.CLA = C_APDU_CLA_DEFAULT;
    command.header.INS = C_APDU_UPDATE_BINARY;
    /* copy the offset */
    command.header.P1 = GETMSB(offset);
    command.header.P2 = GETLSB(offset);
    /* copy the number of byte of the data field */
    command.body.LC = length;
    command.body.data = data;
    /* copy the File Id */
    //memcpy(command.Body.pData ,data, length );
    build_I_block_command(CMD_MASK_UPDATEBINARY, &command, _did_byte, &command_length, _buffer);

    status = io_send_i2c_command(command_length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_updated_binary(this, status, offset, (uint8_t*) data, length);
        return status;
    }

    _last_command = UPDATE;
    _last_command_data.data = (uint8_t*) data;
    _last_command_data.length = length;
    _last_command_data.offset = offset;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_update_binary();
        } else {
            _last_command = NONE;
            get_callback()->on_updated_binary(this, status, offset, (uint8_t*) data, length);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_update_binary() {
    uint8_t response[STATUSRESPONSE_NBBYTE];
    M24srError_t status;
    const uint16_t length = _last_command_data.length;
    uint8_t *data = _last_command_data.data;
    const uint16_t offset = _last_command_data.offset;

    _last_command = NONE;

    status = io_receive_i2c_response(sizeof(response), response);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_updated_binary(this, status, offset, data, length);
        return status;
    }

    if (is_S_block(response) == M24SR_SUCCESS) {
        /* check the CRC */
        status = is_correct_crc_residue(response, WATINGTIMEEXTRESPONSE_NBBYTE);
        // TODO: why if we check ==NFC_Commandsuccess it fail?
        if (status != M24SR_IO_ERROR_CRC) {
            /* send the FrameExension response*/
            status = send_fwt_extension(response[OFFSET_PCB + 1]);
            if (status != M24SR_SUCCESS) { //something get wrong -> abort the update
                get_callback()->on_updated_binary(this, status, offset, data, length);
            }
        }
    } else {
        status = is_correct_crc_residue(response, STATUSRESPONSE_NBBYTE);
        get_callback()->on_updated_binary(this, status, offset, data, length);
    }

    return status;
}

/**
 * @brief This function sends the Verify command
 * @param password_type   PasswordId ( 0x0001 : Read NDEF pwd or 0x0002 : Write NDEF pwd or 0x0003 : I2C pwd)
 * @param password   pointer to the password
 * @retval Status (SW1&SW2) Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::verify(PasswordType_t password_type, const uint8_t *password) {
    C_APDU command;
    M24srError_t status;
    uint16_t length;

    /* check the parameters */
    if (password_type > I2C_PASSWORD) {
        get_callback()->on_verified(this, M24SR_IO_ERROR_PARAMETER, password_type, password);
        return M24SR_IO_ERROR_PARAMETER;
    }

    /* build the command */
    command.header.CLA = C_APDU_CLA_DEFAULT;
    command.header.INS = C_APDU_VERIFY;
    /* copy the Password Id */
    command.header.P1 = GETMSB(password_type);
    command.header.P2 = GETLSB(password_type);
    /* copy the number of bytes of the data field */
    command.body.LC = password ? 0x10 : 0x00;

    if (password) {
        /* copy the password */
        command.body.data = password;
        /* build the I2C command */
        build_I_block_command(CMD_MASK_VERIFYBINARYWITHPWD, &command, _did_byte, &length, _buffer);
    } else {
        command.body.data = NULL;
        /* build the I2C command */
        build_I_block_command(CMD_MASK_VERIFYBINARYWOPWD, &command, _did_byte, &length, _buffer);
    }

    /* send the request */
    status = io_send_i2c_command(length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_verified(this, status, password_type, password);
        return status;
    }
    _last_command = VERIFY;
    _last_command_data.data = (uint8_t*) password;
    _last_command_data.offset = password_type;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_verify();
        } else {
            _last_command = NONE;
            get_callback()->on_verified(this, status, password_type, password);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_verify() {
    M24srError_t status;
    uint8_t respBuffer[STATUSRESPONSE_NBBYTE];
    _last_command = NONE;

    const uint8_t *data = _last_command_data.data;
    const PasswordType_t type = PasswordType_t(_last_command_data.offset);

    status = io_receive_i2c_response(sizeof(respBuffer), respBuffer);

    if (status != M24SR_SUCCESS) {
        get_callback()->on_verified(this, status, type, data);
        return status;
    }

    status = is_correct_crc_residue(respBuffer, STATUSRESPONSE_NBBYTE);
    get_callback()->on_verified(this, status, type, data);
    return status;
}

/**
 * @brief This function sends the ChangeReferenceData command
 * @param password_type PasswordId (0x0001 : Read NDEF pwd or 0x0002 : Write NDEF pwd or 0x0003 : I2C pwd)
 * @param password pointer to the passwaord
 * @retval Status (SW1&SW2) Satus of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::change_reference_data(PasswordType_t password_type, const uint8_t *password) {
    C_APDU command;
    M24srError_t status;
    uint16_t length;

    /* check the parameters */
    if (password_type > I2C_PASSWORD) {
        get_callback()->on_change_reference_data(this, M24SR_IO_ERROR_PARAMETER, password_type, password);
        return M24SR_IO_ERROR_PARAMETER;
    }

    /* build the command */
    command.header.CLA = C_APDU_CLA_DEFAULT;
    command.header.INS = C_APDU_CHANGE;
    /* copy the Password Id */
    command.header.P1 = GETMSB(password_type);
    command.header.P2 = GETLSB(password_type);
    /* copy the number of byte of the data field */
    command.body.LC = PASSWORD_NBBYTE;
    /* copy the password */
    command.body.data = password;
    /* build the command */
    build_I_block_command(CMD_MASK_CHANGEREFDATA, &command, _did_byte, &length, _buffer);

    /* send the request */
    status = io_send_i2c_command(length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_change_reference_data(this, status, password_type, password);
        return status;
    }

    _last_command = CHANGE_REFERENCE_DATA;
    _last_command_data.data = (uint8_t*) password;
    /* use the offset filed for store the pwd type */
    _last_command_data.offset = (uint8_t) password_type;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_change_reference_data();
        } else {
            _last_command = NONE;
            get_callback()->on_change_reference_data(this, status, password_type, password);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_change_reference_data() {
    M24srError_t status;
    uint8_t rensponse[STATUSRESPONSE_NBBYTE];

    PasswordType_t type = PasswordType_t(_last_command_data.offset);
    uint8_t *data = _last_command_data.data;

    status = io_receive_i2c_response(STATUSRESPONSE_NBBYTE, rensponse);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_change_reference_data(this, status, type, data);
        return status;
    }

    status = is_correct_crc_residue(rensponse, STATUSRESPONSE_NBBYTE);
    get_callback()->on_change_reference_data(this, status, type, data);
    return status;
}

/**
 * @brief This function sends the EnableVerificationRequirement command
 * @param password_type enable the read or write protection ( 0x0001 : Read or 0x0002 : Write  )
 * @retval Status (SW1&SW2) Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::enable_verification_requirement(PasswordType_t password_type) {
    C_APDU command;
    M24srError_t status;
    uint16_t length;

    /* check the parameters */
    if ((password_type != READ_PASSWORD) && (password_type != WRITE_PASSWORD)) {
        get_callback()->on_enable_verification_requirement(this, M24SR_IO_ERROR_PARAMETER,
                                                           password_type);
        return M24SR_IO_ERROR_PARAMETER;
    }

    /* build the command */
    command.header.CLA = C_APDU_CLA_DEFAULT;
    command.header.INS = C_APDU_ENABLE;
    /* copy the Password Id */
    command.header.P1 = GETMSB(password_type);
    command.header.P2 = GETLSB(password_type);
    /* build the I2C command */
    build_I_block_command(CMD_MASK_ENABLEVERIFREQ, &command, _did_byte, &length, _buffer);

    /* send the request */
    status = io_send_i2c_command(length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_enable_verification_requirement(this, status, password_type);
        return status;
    }

    _last_command = ENABLE_VERIFICATION_REQUIREMENT;
    //use the offset filed for store the pwd id;
    _last_command_data.offset = (uint8_t) password_type;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_enable_verification_requirement();
        } else {
            _last_command = NONE;
            get_callback()->on_enable_verification_requirement(this, status, password_type);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_enable_verification_requirement() {
    M24srError_t status;
    uint8_t rensponse[STATUSRESPONSE_NBBYTE];

    PasswordType_t type = PasswordType_t(_last_command_data.offset);

    status = io_receive_i2c_response(STATUSRESPONSE_NBBYTE, rensponse);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_enable_verification_requirement(this, status, type);
        return status;
    }

    status = is_correct_crc_residue(rensponse, STATUSRESPONSE_NBBYTE);
    get_callback()->on_enable_verification_requirement(this, status, type);
    return status;
}

/**
 * @brief This function sends the DisableVerificationRequirement command
 * @param password_type enable the read or write protection ( 0x0001 : Read or 0x0002 : Write  )
 * @retval Status (SW1&SW2)   Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::disable_verification_requirement(PasswordType_t password_type) {
    C_APDU command;
    M24srError_t status;
    uint16_t length;

    /* check the parameters */
    if ((password_type != READ_PASSWORD) && (password_type != WRITE_PASSWORD)) {
        get_callback()->on_disable_verification_requirement(this, M24SR_IO_ERROR_PARAMETER, password_type);
        return M24SR_IO_ERROR_PARAMETER;
    }

    /* build the command */
    command.header.CLA = C_APDU_CLA_DEFAULT;
    command.header.INS = C_APDU_DISABLE;
    /* copy the Password Id */
    command.header.P1 = GETMSB(password_type);
    command.header.P2 = GETLSB(password_type);
    /* build the command */
    build_I_block_command(CMD_MASK_DISABLEVERIFREQ, &command, _did_byte, &length, _buffer);

    /* send the request */
    status = io_send_i2c_command(length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_disable_verification_requirement(this, status, password_type);
        return status;
    }

    _last_command = DISABLE_VERIFICATION_REQUIREMENT;
    //use the offset filed for store the pwd id;
    _last_command_data.offset = (uint8_t) password_type;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_disable_verification_requirement();
        } else {
            _last_command = NONE;
            get_callback()->on_disable_verification_requirement(this, status, password_type);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_disable_verification_requirement() {
    M24srError_t status;
    uint8_t rensponse[STATUSRESPONSE_NBBYTE];

    PasswordType_t type = PasswordType_t(_last_command_data.offset);

    status = io_receive_i2c_response(STATUSRESPONSE_NBBYTE, rensponse);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_disable_verification_requirement(this, status, type);
        return status;
    }

    status = is_correct_crc_residue(rensponse, STATUSRESPONSE_NBBYTE);
    get_callback()->on_disable_verification_requirement(this, status, type);
    return status;
}

/**
 * @brief This function sends the EnablePermananentState command
 * @param password_type   enable the read or write protection ( 0x0001 : Read or 0x0002 : Write  )
 * @retval Status (SW1&SW2)   Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::enable_permanent_state(PasswordType_t password_type) {
    C_APDU command;
    M24srError_t status;
    uint16_t length;

    /* check the parameters */
    if ((password_type != READ_PASSWORD) && (password_type != WRITE_PASSWORD)) {
        get_callback()->on_enable_permanent_state(this, M24SR_IO_ERROR_PARAMETER, password_type);
        return M24SR_IO_ERROR_PARAMETER;
    }

    /* build the command */
    command.header.CLA = C_APDU_CLA_ST;
    command.header.INS = C_APDU_ENABLE;
    /* copy the Password Id */
    command.header.P1 = GETMSB(password_type);
    command.header.P2 = GETLSB(password_type);
    /* build the I2C command */
    build_I_block_command(CMD_MASK_ENABLEVERIFREQ, &command, _did_byte, &length, _buffer);

    /* send the request */
    status = io_send_i2c_command(length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_enable_permanent_state(this, status, password_type);
        return status;
    }

    _last_command = ENABLE_PERMANET_STATE;
    //use the offset filed for store the pwd id;
    _last_command_data.offset = (uint8_t) password_type;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_enable_permanent_state();
        } else {
            _last_command = NONE;
            get_callback()->on_enable_permanent_state(this, status, password_type);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_enable_permanent_state() {
    M24srError_t status;
    uint8_t rensponse[STATUSRESPONSE_NBBYTE];

    PasswordType_t type = PasswordType_t(_last_command_data.offset);

    status = io_receive_i2c_response(STATUSRESPONSE_NBBYTE, rensponse);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_enable_permanent_state(this, status, type);
        return status;
    }

    status = is_correct_crc_residue(rensponse, STATUSRESPONSE_NBBYTE);
    get_callback()->on_enable_permanent_state(this, status, type);
    return status;
}

/**
 * @brief This function sends the DisablePermanentState command
 * @param password_type enable the read or write protection ( 0x0001 : Read or 0x0002 : Write  )
 * @retval Status (SW1&SW2)   Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::disable_permanent_state(PasswordType_t password_type) {
    C_APDU command;
    M24srError_t status;
    uint16_t length;

    /* check the parameters */
    if ((password_type != READ_PASSWORD) && (password_type != WRITE_PASSWORD)) {
        get_callback()->on_disable_permanent_state(this, M24SR_IO_ERROR_PARAMETER, password_type);
        return M24SR_IO_ERROR_PARAMETER;
    }

    /* build the command */
    command.header.CLA = C_APDU_CLA_ST;
    command.header.INS = C_APDU_DISABLE;
    /* copy the Password Id */
    command.header.P1 = GETMSB(password_type);
    command.header.P2 = GETLSB(password_type);
    /* build the I2C command */
    build_I_block_command(CMD_MASK_DISABLEVERIFREQ, &command, _did_byte, &length, _buffer);

    /* send the request */
    status = io_send_i2c_command(length, _buffer);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_enable_permanent_state(this, status, password_type);
        return status;
    }

    _last_command = DISABLE_PERMANET_STATE;
    //use the offset filed for store the pwd id;
    _last_command_data.offset = (uint8_t) password_type;

    if (_communication_type == SYNC) {
        status = io_poll_i2c();
        if (status == M24SR_SUCCESS) {
            return receive_disable_permanent_state();
        } else {
            _last_command = NONE;
            get_callback()->on_disable_permanent_state(this, status, password_type);
            return status;
        }
    }

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::receive_disable_permanent_state() {
    M24srError_t status;
    uint8_t rensponse[STATUSRESPONSE_NBBYTE];

    PasswordType_t type = PasswordType_t(_last_command_data.offset);

    status = io_receive_i2c_response(STATUSRESPONSE_NBBYTE, rensponse);
    if (status != M24SR_SUCCESS) {
        get_callback()->on_disable_permanent_state(this, status, type);
        return status;
    }

    status = is_correct_crc_residue(rensponse, STATUSRESPONSE_NBBYTE);
    get_callback()->on_disable_permanent_state(this, status, type);
    return status;
}

/**
 * @brief This function generates an interrupt on GPO pin
 * @retval Status (SW1&SW2)   Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::send_interrupt() {
    C_APDU command;
    uint16_t length;

    M24srError_t status = manage_i2c_gpo(INTERRUPT);
    if (status != M24SR_SUCCESS) {
        return status;
    }

    /* build the command */
    command.header.CLA = C_APDU_CLA_ST;
    command.header.INS = C_APDU_INTERRUPT;
    /* copy the Password Id */
    command.header.P1 = 0x00;
    command.header.P2 = 0x1E;
    command.body.LC = 0x00;
    /* build the I2C command */
    build_I_block_command(CMD_MASK_SENDINTERRUPT, &command, _did_byte, &length, _buffer);

    return send_receive_i2c(length, _buffer);
}

M24srError_t M24srDriver::send_receive_i2c(uint16_t length, uint8_t *buffer) {
    M24srError_t status;

    /* send the request */
    status = io_send_i2c_command(length, buffer);
    if (status != M24SR_SUCCESS)
        return status;

    status = io_poll_i2c();
    if (status != M24SR_SUCCESS)
        return status;

    /* read the response */
    status = io_receive_i2c_response(STATUSRESPONSE_NBBYTE, buffer);
    if (status != M24SR_SUCCESS)
        return status;

    return is_correct_crc_residue(buffer, STATUSRESPONSE_NBBYTE);
}

/**
 * @brief This function forces GPO pin to low state or high Z
 * @param uSetOrReset select if GPO must be low (reset) or HiZ
 * @retval Status (SW1&SW2) Status of the operation to complete.
 * @retval M24SR_ERROR_I2CTIMEOUT I2C timeout occurred.
 */
M24srError_t M24srDriver::state_control(bool gpo_reset) {
    C_APDU command;
    uint16_t length;

    M24srError_t status = manage_i2c_gpo(STATE_CONTROL);
    if (status == M24SR_SUCCESS) {
        return status;
    }

    uint8_t reset = (uint8_t)gpo_reset;

    /* build the command */
    command.header.CLA = C_APDU_CLA_ST;
    command.header.INS = C_APDU_INTERRUPT;
    /* copy the Password Id */
    command.header.P1 = 0x00;
    command.header.P2 = 0x1F;
    command.body.LC = 0x01;
    command.body.data = &reset;
    /* build the I2C command */
    build_I_block_command(CMD_MASK_GPOSTATE, &command, _did_byte, &length, _buffer);

    return send_receive_i2c(length, _buffer);
}

M24srError_t M24srDriver::manage_i2c_gpo(NfcGpoState_t gpo_i2c_config) {
    if (_gpo_pin.is_connected() == 0) {
        return M24SR_IO_PIN_NOT_CONNECTED;
    }

    if (gpo_i2c_config > STATE_CONTROL) {
        return M24SR_IO_ERROR_PARAMETER;
    }

    //enable the callback for change the gpo
    _component_cb = &_manage_gpo_cb;
    _manage_gpo_cb.set_new_gpo_config(true, gpo_i2c_config);

    //start the manageGPO procedure
    return select_application();
}

M24srError_t M24srDriver::manage_rf_gpo(NfcGpoState_t gpo_rf_config) {
    if (_rf_disable_pin.is_connected() == 0) {
        return M24SR_IO_PIN_NOT_CONNECTED;
    }

    if (gpo_rf_config > STATE_CONTROL) {
        return M24SR_IO_ERROR_PARAMETER;
    }

    _component_cb = &_manage_gpo_cb;
    _manage_gpo_cb.set_new_gpo_config(false, gpo_rf_config);

    return select_application();
}

M24srError_t M24srDriver::rf_config(bool enable) {
    if (_rf_disable_pin.is_connected() == 0) {
        return M24SR_IO_PIN_NOT_CONNECTED;
    }
    /* invert since it's a disable pin */
    _rf_disable_pin = !enable;

    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::io_send_i2c_command(uint8_t length, const uint8_t *buffer) {
    int ret = _i2c_channel.write(M24SR_ADDR, (const char*) buffer, length);
    if (ret == 0) {
        return M24SR_SUCCESS;
    }
    return M24SR_IO_ERROR_I2CTIMEOUT;
}

M24srError_t M24srDriver::io_receive_i2c_response(uint8_t length, uint8_t *buffer) {
    int ret = _i2c_channel.read(M24SR_ADDR, (char*) buffer, length);
    if (ret == 0) {
        return M24SR_SUCCESS;
    }

    return M24SR_IO_ERROR_I2CTIMEOUT;
}

M24srError_t M24srDriver::io_poll_i2c() {
    int status = 1;
    while (status != 0) {
        //send the device address and wait to receive an ack bit
        status = _i2c_channel.write(M24SR_ADDR, NULL, 0);
    }
    return M24SR_SUCCESS;
}

M24srError_t M24srDriver::manage_event() {
    switch (_last_command) {
    case DESELECT:
        return receive_deselect();
    case SELECT_APPLICATION:
        return receive_select_application();
    case SELECT_CC_FILE:
        return receive_select_cc_file();
    case SELECT_NDEF_FILE:
        return receive_select_ndef_file();
    case SELECT_SYSTEM_FILE:
        return receive_select_system_file();
    case READ:
        return receive_read_binary();
    case UPDATE:
        return receive_update_binary();
    case VERIFY:
        return receive_verify();
    case CHANGE_REFERENCE_DATA:
        return receive_change_reference_data();
    case ENABLE_VERIFICATION_REQUIREMENT:
        return receive_enable_verification_requirement();
    case DISABLE_VERIFICATION_REQUIREMENT:
        return receive_disable_verification_requirement();
    case ENABLE_PERMANET_STATE:
        return receive_enable_permanent_state();
    case DISABLE_PERMANET_STATE:
        return receive_disable_permanent_state();
    default:
        return M24SR_SUCCESS;
    }
}

} //nfc
} //vendor
} //ST

/******************* (C) COPYRIGHT 2013 STMicroelectronics *****END OF FILE****/
