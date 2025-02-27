#include "btc_types.h"
#include "../../core/client/request.h"
#include "../../core/client/request_internal.h"
#include "../../core/util/mem.h"
#include "../../core/util/utils.h"
#include "btc_script.h"
#include "btc_serialize.h"

typedef enum btc_tx_field {
  BTC_INPUT,
  BTC_OUTPUT,
  BTC_WITNESS
} btc_tx_field_t;

bool pub_key_is_valid(const bytes_t* pub_key) {
  return (pub_key->len == BTC_UNCOMP_PUB_KEY_SIZE_BYTES && pub_key->data[0] == 0x4) || (pub_key->len == BTC_COMP_PUB_KEY_SIZE_BYTES && (pub_key->data[0] == 0x2 || pub_key->data[0] == 0x3));
}

void btc_init_tx(btc_tx_t* tx) {
  if (tx) {
    memset(tx, 0, sizeof(btc_tx_t));
    tx->version = 1;
  }
}

void btc_init_tx_ctx(btc_tx_ctx_t* tx_ctx) {
  if (tx_ctx) {
    memset(tx_ctx, 0, sizeof(btc_tx_ctx_t));
    btc_init_tx(&tx_ctx->tx);
  }
}

void btc_init_tx_in(btc_tx_in_t* tx_in) {
  if (tx_in) {
    memset(tx_in, 0, sizeof(btc_tx_in_t));
    tx_in->sequence = DEFAULT_TXIN_SEQUENCE_NUMBER;
  }
}

void btc_init_tx_out(btc_tx_out_t* tx_out) {
  if (tx_out) {
    memset(tx_out, 0, sizeof(btc_tx_out_t));
  }
}

void btc_init_utxo(btc_utxo_t* utxo) {
  if (utxo) {
    memset(utxo, 0, sizeof(btc_utxo_t));
    utxo->sequence = DEFAULT_TXIN_SEQUENCE_NUMBER;
  }
}

void btc_free_tx(btc_tx_t* tx) {
  if (tx) {
    if (tx->all.data) _free(tx->all.data);
    if (tx->input.data) _free(tx->input.data);
    if (tx->output.data) _free(tx->output.data);
    if (tx->witnesses.data) _free(tx->witnesses.data);
  }
}

void btc_free_tx_in(btc_tx_in_t* tx_in) {
  if (tx_in) {
    if (tx_in->prev_tx_hash) _free(tx_in->prev_tx_hash);
    if (tx_in->script.data.data) _free(tx_in->script.data.data);
  }
}

void btc_free_tx_out(btc_tx_out_t* tx_out) {
  if (tx_out && tx_out->script.data.data) _free(tx_out->script.data.data);
}

void btc_free_utxo(btc_utxo_t* utxo) {
  if (utxo) {
    if (utxo->tx_hash) _free(utxo->tx_hash);
    if (utxo->raw_script.data.data) _free(utxo->raw_script.data.data);

    btc_free_tx_out(&utxo->tx_out);

    if (utxo->signatures) {
      for (uint32_t i = 0; i < utxo->sig_count; i++) {
        _free(utxo->signatures[i].data);
      }
      _free(utxo->signatures);
    }

    if (utxo->accounts) {
      for (uint32_t i = 0; i < utxo->accounts_count; i++) {
        _free(utxo->accounts[i].pub_key.data);
        _free(utxo->accounts[i].account.data);
      }
      _free(utxo->accounts);
    }
  }
}

void btc_free_tx_ctx(btc_tx_ctx_t* tx_ctx) {
  if (tx_ctx) {
    btc_free_tx(&tx_ctx->tx);
    if (tx_ctx->utxos) {
      for (uint32_t i = 0; i < tx_ctx->utxo_count; i++) {
        btc_free_utxo(&tx_ctx->utxos[i]);
      }
      _free(tx_ctx->utxos);
    }
    if (tx_ctx->inputs) {
      for (uint32_t i = 0; i < tx_ctx->input_count; i++) {
        btc_free_tx_in(&tx_ctx->inputs[i]);
      }
      _free(tx_ctx->inputs);
    }
    if (tx_ctx->outputs) {
      for (uint32_t i = 0; i < tx_ctx->output_count; i++) {
        btc_free_tx_out(&tx_ctx->outputs[i]);
      }
      _free(tx_ctx->outputs);
    }
  }
}

uint8_t* btc_parse_tx_in(uint8_t* data, btc_tx_in_t* dst, uint8_t* limit) {
  uint64_t len;
  dst->prev_tx_hash     = data;
  dst->prev_tx_index    = le_to_int(data + BTC_TX_HASH_SIZE_BYTES);
  dst->script.data.data = data + BTC_TX_IN_PREV_OUPUT_SIZE_BYTES + decode_var_int(data + BTC_TX_IN_PREV_OUPUT_SIZE_BYTES, &len);
  dst->script.data.len  = (uint32_t) len;
  if (dst->script.data.data + dst->script.data.len + 4 > limit) return NULL; // check limit
  dst->sequence = le_to_int(dst->script.data.data + dst->script.data.len);
  return dst->script.data.data + dst->script.data.len + 4;
}

// WARNING: You need to free dst.data after calling this function
// TODO: Implement support for "Coinbase" inputs
// TODO: Handle null arguments
// TODO: Handle max script len = 10000 bytes
in3_ret_t btc_serialize_tx_in(in3_req_t* req, btc_tx_in_t* tx_in, bytes_t* dst) {
  if (!tx_in || !dst) return req_set_error(req, "ERROR: in btc_serialize_tx_in: Arguments cannot be null", IN3_EINVAL);
  if (!tx_in->prev_tx_hash) return req_set_error(req, "ERROR: in btc_serialize_tx_in: missing previous transaction hash", IN3_ERPC);

  // calculate serialized tx input size in bytes
  uint32_t tx_in_size = (BTC_TX_IN_PREV_OUPUT_SIZE_BYTES +
                         get_compact_uint_size((uint64_t) tx_in->script.data.len) +
                         tx_in->script.data.len +
                         BTC_TX_IN_SEQUENCE_SIZE_BYTES);

  // serialize tx_in
  dst->data = (dst->data) ? _realloc(&dst->data, tx_in_size, dst->len) : _malloc(tx_in_size);
  dst->len  = tx_in_size;

  uint32_t index = 0;
  rev_copyl(dst->data + index, bytes(tx_in->prev_tx_hash, BTC_TX_HASH_SIZE_BYTES), BTC_TX_HASH_SIZE_BYTES);
  index += BTC_TX_HASH_SIZE_BYTES;
  uint_to_le(dst, index, tx_in->prev_tx_index);
  index += BTX_TX_INDEX_SIZE_BYTES;

  // -- script
  long_to_compact_uint(dst, index, tx_in->script.data.len);
  index += get_compact_uint_size(tx_in->script.data.len);
  memcpy(dst->data + index, tx_in->script.data.data, tx_in->script.data.len);

  // -- sequence
  uint_to_le(dst, index, tx_in->sequence);
  return IN3_OK;
}

uint8_t* btc_parse_tx_out(uint8_t* data, btc_tx_out_t* dst) {
  uint64_t len;
  dst->value            = le_to_long(data);
  dst->script.data.data = data + BTC_TX_OUT_VALUE_SIZE_BYTES + decode_var_int(data + BTC_TX_OUT_VALUE_SIZE_BYTES, &len);
  dst->script.data.len  = (uint32_t) len;
  return dst->script.data.data + dst->script.data.len;
}

// WARNING: You need to free 'dst' pointer after calling this function
// TODO: Handle null arguments
in3_ret_t btc_serialize_tx_out(in3_req_t* req, btc_tx_out_t* tx_out, bytes_t* dst) {
  if (!tx_out || !dst) return req_set_error(req, "ERROR: in btc_serialize_tx_out: Arguments cannot be null", IN3_EINVAL);
  if (tx_out->script.data.len > MAX_SCRIPT_SIZE_BYTES) return req_set_error(req, "ERROR: in btc_serialize_tx_out: Script is bigger than the maximum allowed size", IN3_ENOTSUP);

  // calculate serialized tx output size in bytes
  uint32_t tx_out_size = (BTC_TX_OUT_VALUE_SIZE_BYTES +
                          get_compact_uint_size((uint64_t) tx_out->script.data.len) +
                          tx_out->script.data.len);

  // alloc memory in dst
  dst->data = (dst->data) ? _realloc(&dst->data, tx_out_size, dst->len) : _malloc(tx_out_size);
  dst->len  = tx_out_size;

  // serialize tx_out
  uint32_t index = 0;

  // -- value
  long_to_le(dst, index, tx_out->value);
  index += BTC_TX_OUT_VALUE_SIZE_BYTES;

  // -- lock-script size
  long_to_compact_uint(dst, index, tx_out->script.data.len);
  index += get_compact_uint_size((uint64_t) tx_out->script.data.len);

  // -- lock-script
  memcpy(dst->data + index, tx_out->script.data.data, tx_out->script.data.len);
  return IN3_OK;
}

in3_ret_t btc_parse_tx(bytes_t tx, btc_tx_t* dst) {
  uint64_t     val;
  btc_tx_in_t  tx_in;
  btc_tx_out_t tx_out;
  dst->all     = tx;
  dst->version = le_to_int(tx.data);
  dst->flag    = btc_is_witness(tx) ? 1 : 0;
  uint8_t* end = tx.data + tx.len;
  uint8_t* p   = tx.data + (dst->flag ? 6 : 4);

  p += decode_var_int(p, &val);
  if (p >= end) return IN3_EINVAL;
  dst->input_count = (uint32_t) val;
  dst->input.data  = p;
  for (uint32_t i = 0; i < dst->input_count; i++) {
    p = btc_parse_tx_in(p, &tx_in, end);
    if (!p || p >= end) return IN3_EINVAL;
  }
  dst->input.len = p - dst->input.data;

  p += decode_var_int(p, &val);
  dst->output_count = (uint32_t) val;
  dst->output.data  = p;
  for (uint32_t i = 0; i < dst->output_count; i++) {
    p = btc_parse_tx_out(p, &tx_out);
    if (p > end) return IN3_EINVAL;
  }
  dst->output.len = p - dst->output.data;
  dst->witnesses  = bytes(p, tx.data + tx.len - 4 - p);
  dst->lock_time  = le_to_int(tx.data + tx.len - 4);

  return IN3_OK;
}

uint32_t btc_get_raw_tx_size(const btc_tx_t* tx) {
  return (BTC_TX_VERSION_SIZE_BYTES +
          (2 * tx->flag) +
          get_compact_uint_size((uint64_t) tx->input_count) +
          tx->input.len +
          get_compact_uint_size((uint64_t) tx->output_count) +
          tx->output.len +
          (tx->flag ? tx->witnesses.len : 0) +
          BTC_TX_LOCKTIME_SIZE_BYTES);
}

// Converts a btc transaction into a serialized transaction
// WARNING: You need to free dst pointer after using this function!
in3_ret_t btc_serialize_tx(in3_req_t* req, const btc_tx_t* tx, bytes_t* dst) {
  if (!tx || !dst) return req_set_error(req, "ERROR: in btc_serialize_tx: Arguments cannot be null", IN3_EINVAL);
  if (tx->input.len == 0 || tx->output.len == 0) return req_set_error(req, "ERROR: in btc_serialize_tx: Transaction inputs or outputs missing", IN3_EINVAL);
  if (tx->flag && tx->witnesses.len == 0) return req_set_error(req, "ERROR: in btc_serialize_tx: Missing witness data in a segwit transaction", IN3_EINVAL);

  // calculate transaction size in bytes
  uint32_t tx_size = btc_get_raw_tx_size(tx);

  dst->data = dst->data ? _realloc(dst->data, tx_size, dst->len) : _malloc(tx_size);
  dst->len  = tx_size;

  // Serialize transaction data
  uint32_t index = 0;

  // version
  uint_to_le(dst, index, tx->version);
  index += BTC_TX_VERSION_SIZE_BYTES;

  // Check if transaction uses SegWit
  if (tx->flag) {
    dst->data[index++] = 0;
    dst->data[index++] = 1;
  }

  // input_count
  long_to_compact_uint(dst, index, tx->input_count);
  index += get_compact_uint_size(tx->input_count);

  // inputs
  memcpy(dst->data + index, tx->input.data, tx->input.len);
  index += tx->input.len;

  // output_count
  long_to_compact_uint(dst, index, tx->output_count);
  index += get_compact_uint_size(tx->output_count);

  // outputs
  memcpy(dst->data + index, tx->output.data, tx->output.len);
  index += tx->output.len;

  // witnesses
  if (tx->flag) {
    memcpy(dst->data + index, tx->witnesses.data, tx->witnesses.len);
    index += tx->output.len;
  }

  // locktime
  dst->data[index + 3] = ((tx->lock_time >> 24) & 0xff);
  dst->data[index + 2] = ((tx->lock_time >> 16) & 0xff);
  dst->data[index + 1] = ((tx->lock_time >> 8) & 0xff);
  dst->data[index]     = ((tx->lock_time) & 0xff);

  return IN3_OK;
}

uint32_t btc_vsize(btc_tx_t* tx) {
  uint32_t w = btc_weight(tx);
  return w % 4 ? (w + 4) / 4 : w / 4;
}

uint32_t btc_weight(btc_tx_t* tx) {
  const uint32_t w = tx->witnesses.len
                         ? (tx->all.len - tx->witnesses.len - 2) * 3 + tx->all.len
                         : tx->all.len * 4;
  return w;
}

in3_ret_t btc_tx_id(btc_tx_t* tx, bytes32_t dst) {
  bytes_t  data;
  uint8_t* start = tx->all.data + (tx->flag ? 6 : 4);
  data.len       = tx->output.len + tx->output.data - start + 8;
  data.data      = data.len > 1000 ? _malloc(data.len) : alloca(data.len);
  memcpy(data.data, tx->all.data, 4);                                  // nVersion
  memcpy(data.data + 4, start, data.len - 8);                          // txins/txouts
  memcpy(data.data + data.len - 4, tx->all.data + tx->all.len - 4, 4); // lockTime

  btc_hash(data, dst);
  if (data.len > 1000) _free(data.data);
  return IN3_OK;
}

bool btc_public_key_is_valid(const bytes_t* public_key) {
  return (((public_key->len == 33) && (public_key->data[0] == 0x2 || public_key->data[0] == 0x3)) ||
          (public_key->len == 65 && public_key->data[0] == 0x4));
}

uint32_t extract_public_keys_from_multisig(bytes_t multisig_script, bytes_t** pub_key_list_dst) {
  if (!is_p2ms(&multisig_script)) return 0;

  uint32_t pub_key_count = btc_get_multisig_pub_key_count(&multisig_script);

  // alloc memory and in array of pub keys
  bytes_t* pub_key_list = _malloc(pub_key_count * sizeof(bytes_t));

  // Extract pubKeys and convert each one to an address
  uint8_t* p = multisig_script.data;
  uint32_t pklen;
  for (uint32_t i = 0; i < pub_key_count; i++) {
    // extract pubKey from script
    p++;
    pklen           = *p;
    bytes_t pub_key = bytes(_malloc(pklen), pklen);
    memcpy(pub_key.data, p + 1, pklen);
    p += pklen;

    // write public key into array
    pub_key_list[i] = pub_key;
  }
  *pub_key_list_dst = pub_key_list;
  return pub_key_count;
}

// WARNING: You should free dst.encoded after calling this function
// WARNING: P2WPKH and P2WSH scripts still not supported
// Returns BTC_UNKNOWN when something goes wrong
btc_stype_t extract_address_from_output(btc_tx_out_t* tx_out, btc_address_t* dst) {
  if (!tx_out || !dst) return BTC_UNKNOWN;
  btc_stype_t script_type = (tx_out->script.type == BTC_UNKNOWN) ? btc_get_script_type(&tx_out->script.data) : tx_out->script.type;

  switch (script_type) {
    case BTC_P2PK: {
      // extract raw PubKey from script
      bytes_t pub_key = bytes(tx_out->script.data.data + 1, (uint32_t) tx_out->script.data.data[0]);
      if (pub_key_is_valid(&pub_key))
        // use public key to calculate address
        btc_addr_from_pub_key(pub_key, BTC_P2PKH_PREFIX, dst);
      break;
    }
    case BTC_P2PKH: {
      ripemd160_t pkhash;
      memcpy(pkhash, tx_out->script.data.data + 3, BTC_HASH160_SIZE_BYTES);
      btc_addr_from_pub_key_hash(pkhash, BTC_P2PKH_PREFIX, dst);
      break;
    }
    case BTC_P2SH: {
      ripemd160_t script_hash;
      memcpy(script_hash, tx_out->script.data.data + 2, BTC_HASH160_SIZE_BYTES);
      btc_addr_from_pub_key_hash(script_hash, BTC_P2SH_PREFIX, dst);
      break;
    }
    case BTC_P2MS:
      // P2MS transactions do not have any intrinsic addresses
      // Should call extract_public_keys_from_multisig
      // Only return the script type
      break;
    case BTC_V0_P2WPKH: {
      ripemd160_t pkhash;
      memcpy(pkhash, tx_out->script.data.data + 2, BTC_HASH160_SIZE_BYTES);
      btc_segwit_addr_from_pub_key_hash(pkhash, dst);
      break;
    }
    case BTC_P2WSH:
      btc_segwit_addr_from_witness_program(tx_out->script.data, dst);
      break;
    default:
      return BTC_UNSUPPORTED;
  }
  return script_type;
}

static in3_ret_t add_to_tx(in3_req_t* req, btc_tx_ctx_t* tx_ctx, void* src, btc_tx_field_t field_type) {
  if (!tx_ctx || !src) {
    return req_set_error(req, "ERROR: in add_to_tx: Function arguments cannot be null!", IN3_EINVAL);
  }

  bytes_t  raw_src = NULL_BYTES, *dst;
  uint32_t old_len;
  bool     must_free = false;

  switch (field_type) {
    case BTC_INPUT:
      TRY(btc_serialize_tx_in(req, (btc_tx_in_t*) src, &raw_src))
      old_len             = tx_ctx->tx.input.len;
      dst                 = &tx_ctx->tx.input;
      tx_ctx->input_count = tx_ctx->tx.input_count;
      tx_ctx->tx.input_count++;
      tx_ctx->inputs = tx_ctx->inputs ? _realloc(tx_ctx->inputs, tx_ctx->input_count * sizeof(btc_tx_in_t), (tx_ctx->input_count + 1) * sizeof(btc_tx_in_t)) : _malloc(sizeof(btc_tx_in_t));
      tx_ctx->input_count++;
      must_free = true;
      break;
    case BTC_OUTPUT:
      TRY(btc_serialize_tx_out(req, (btc_tx_out_t*) src, &raw_src))
      old_len = tx_ctx->tx.output.len;
      dst     = &tx_ctx->tx.output;
      tx_ctx->tx.output_count++;
      tx_ctx->outputs = tx_ctx->outputs ? _realloc(tx_ctx->outputs, tx_ctx->output_count * sizeof(btc_tx_out_t), (tx_ctx->output_count + 1) * sizeof(btc_tx_out_t)) : _malloc(sizeof(btc_tx_out_t));
      tx_ctx->output_count++;
      must_free = true;
      break;
    case BTC_WITNESS:
      old_len      = tx_ctx->tx.witnesses.len;
      dst          = &tx_ctx->tx.witnesses;
      raw_src.len  = ((bytes_t*) src)->len;
      raw_src.data = ((bytes_t*) src)->data;
      break;
    default:
      return req_set_error(req, "ERROR: in add_to_tx: Unrecognized transaction field code.", IN3_EINVAL);
  }

  dst->len += raw_src.len;
  if (raw_src.data) {
    dst->data = (dst->data) ? _realloc(dst->data, dst->len, old_len) : _malloc(dst->len);
    memcpy(dst->data + old_len, raw_src.data, raw_src.len);
  }
  else {
    dst->data = NULL;
  }

  if (must_free) {
    _free(raw_src.data);
  }
  return IN3_OK;
}

in3_ret_t btc_add_input_to_tx(in3_req_t* req, btc_tx_ctx_t* tx_ctx, btc_tx_in_t* tx_in) {
  return add_to_tx(req, tx_ctx, tx_in, BTC_INPUT);
}

in3_ret_t btc_add_output_to_tx(in3_req_t* req, btc_tx_ctx_t* tx_ctx, btc_tx_out_t* tx_out) {
  return add_to_tx(req, tx_ctx, tx_out, BTC_OUTPUT);
}

in3_ret_t btc_add_witness_to_tx(in3_req_t* req, btc_tx_ctx_t* tx_ctx, bytes_t* witness) {
  return add_to_tx(req, tx_ctx, witness, BTC_WITNESS);
}

in3_ret_t add_outputs_to_tx(in3_req_t* req, d_token_t* outputs, btc_tx_ctx_t* tx_ctx) {
  uint32_t len = d_len(outputs);
  for (uint32_t i = 0; i < len; i++) {
    d_token_t* output = d_get_at(outputs, i);
    if (!output) return req_set_error(req, "ERROR: Transaction output data is missing", IN3_EINVAL);
    const char* script_string = d_string(d_get(output, key("script")));
    if (!script_string) return req_set_error(req, "ERROR: Transaction output script is missing", IN3_EINVAL);
    uint64_t value = d_get_long(output, key("value"));

    btc_tx_out_t tx_out;
    uint32_t     script_len = strlen(script_string) / 2;
    bytes_t      script     = bytes(_malloc(script_len), script_len);
    hex_to_bytes(script_string, strlen(script_string), script.data, script.len);

    tx_out.script.data = script;
    tx_out.script.type = btc_get_script_type(&tx_out.script.data);
    tx_out.value       = value;

    TRY_FINAL(btc_add_output_to_tx(req, tx_ctx, &tx_out), _free(script.data);)
  }
  return IN3_OK;
}

uint32_t btc_build_nsequence_relative_locktime(uint8_t locktime_type_flag, uint16_t value) {

  uint32_t rlt = 0;

  // when flag is:
  // SET: Value represents units of 512 seconds
  // NOT SET: Value represents number of blocks
  if (locktime_type_flag > 0) rlt = SEQUENCE_LOCKTIME_TYPE_FLAG;

  // Add value to the end of relative locktime
  rlt += value;

  return rlt;
}

bool btc_nsequence_is_relative_locktime(uint32_t nsequence) {
  return !(nsequence & SEQUENCE_LOCKTIME_DISABLE_FLAG);
}

uint16_t btc_nsequence_get_relative_locktime_value(uint32_t nsequence) {
  uint16_t value = 0;
  if (btc_nsequence_is_relative_locktime(nsequence)) value = nsequence & SEQUENCE_LOCKTIME_MASK;
  return value;
}

bytes_t btc_build_locking_script(bytes_t* receiving_btc_addr, btc_stype_t type, const bytes_t* args, uint32_t args_len) {
  // TODO: Implement support to scripts of types other than P2PKH
  UNUSED_VAR(args);
  UNUSED_VAR(args_len);
  if (type == BTC_UNKNOWN || type == BTC_NON_STANDARD || type == BTC_UNSUPPORTED || receiving_btc_addr->len < 20) {
    return NULL_BYTES;
  }
  bytes_t locking_script;
  switch (type) {
    case BTC_P2PKH:
      locking_script.len     = 25;
      locking_script.data    = _malloc(locking_script.len);
      locking_script.data[0] = OP_DUP;
      locking_script.data[1] = OP_HASH160;
      locking_script.data[2] = BTC_HASH160_SIZE_BYTES;
      memcpy(locking_script.data + 3, receiving_btc_addr, BTC_HASH160_SIZE_BYTES);
      locking_script.data[23] = OP_EQUALVERIFY;
      locking_script.data[24] = OP_CHECKSIG;
      break;
    default:
      locking_script = NULL_BYTES;
  }
  return locking_script;
}

in3_ret_t btc_prepare_outputs(in3_req_t* req, btc_tx_ctx_t* tx_ctx, d_token_t* output_data) {
  if (!tx_ctx || !output_data) return req_set_error(req, "ERROR: in btc_prepare_outputs: function arguments cannot be null", IN3_EINVAL);
  if (d_type(output_data) != T_ARRAY) return req_set_error(req, "ERROR: in btc_prepare_outputs: invalid output data format", IN3_EINVAL);

  uint32_t output_count = d_len(output_data);
  for (uint32_t i = 0; i < output_count; i++) {
    btc_tx_out_t new_tx_out;
    d_token_t*   output_item = d_get_at(output_data, i);
    if (d_type(output_item) != T_OBJECT) return req_set_error(req, "ERROR: invalid output data format", IN3_EINVAL);

    bytes_t  btc_addr = d_bytes(d_get(output_data, key("address")));
    uint64_t value    = d_get_long(output_data, key("value"));
    if (btc_addr.len != 20) return req_set_error(req, "ERROR: one or more outputs have invalid address", IN3_EINVAL);
    if (value == 0) return req_set_error(req, "ERROR: output value cannot be zero", IN3_EINVAL);

    new_tx_out.value       = value;
    new_tx_out.script.data = btc_build_locking_script(&btc_addr, BTC_P2PKH, NULL, 0);
    new_tx_out.script.type = btc_get_script_type(&new_tx_out.script.data);

    TRY(btc_add_output_to_tx(req, tx_ctx, &new_tx_out))
  }
  return IN3_OK;
}

static void add_account_pub_key_to_utxo(btc_utxo_t* utxo, btc_account_pub_key_t* acc_pk) {
  size_t current_size                  = utxo->accounts_count * sizeof(btc_account_pub_key_t);
  size_t new_size                      = current_size + sizeof(btc_account_pub_key_t);
  utxo->accounts                       = utxo->accounts ? _realloc(utxo->accounts, new_size, current_size) : _malloc(new_size);
  utxo->accounts[utxo->accounts_count] = *acc_pk;
  utxo->accounts_count++;
}

static in3_ret_t handle_utxo_arg(btc_utxo_t* utxo, d_token_t* arg) {
  if (!arg) return IN3_OK;
  if (!utxo) return IN3_EINVAL;
  if (d_type(arg) != T_OBJECT) return IN3_EINVAL;

  // Check for relative locktime (BIP68)
  d_token_t* rlt = d_get(arg, key("rlt"));
  if (rlt) {
    const char* rlt_type = d_get_string(rlt, key("rlt_type"));
    uint16_t    value    = (uint16_t) d_get_long(rlt, key("value"));

    uint8_t rlt_type_flag = 0;
    if (strstr(rlt_type, "block"))
      rlt_type_flag = SEQUENCE_LOCKTIME_TYPE_BLOCK;
    else if (strstr(rlt_type, "time"))
      rlt_type_flag = SEQUENCE_LOCKTIME_TYPE_TIME;
    else
      return IN3_EINVAL;

    utxo->sequence = btc_build_nsequence_relative_locktime(rlt_type_flag, value);
  }
  else {
    utxo->sequence = DEFAULT_TXIN_SEQUENCE_NUMBER;
  }

  // Check for unsupported scripts on utxo
  btc_stype_t script_type = utxo->tx_out.script.type;
  utxo->raw_script.type   = BTC_UNKNOWN;

  if (script_type == BTC_UNKNOWN || script_type == BTC_NON_STANDARD || script_type == BTC_UNSUPPORTED) {
    return IN3_EINVAL;
  }

  // Check for raw scripts on pay-to-script-hash utxo
  if (script_type == BTC_P2SH || script_type == BTC_P2WSH) {
    // is the argument defining an unlocking script?
    bytes_t raw_script = d_bytes(d_get(arg, key("script")));
    if (!raw_script.data) return IN3_EINVAL; // A script should be difined to redeem a utxo of this type
    utxo->raw_script.data = raw_script;
    utxo->raw_script.type = btc_get_script_type(&raw_script);
  }

  // Check for multisig
  if (script_type == BTC_P2MS || utxo->raw_script.type == BTC_P2MS) {
    // is the argument defining a new "account<->pub_key" pair?
    d_token_t* accs = d_get(arg, key("accounts"));
    if (!accs || d_type(accs) != T_ARRAY) return IN3_EINVAL;

    // cleanup accounts data on utxo
    uint32_t accs_len = d_len(accs);

    // include all provided accounts on our utxo
    for (uint32_t i = 0; i < accs_len; i++) {
      btc_account_pub_key_t acc_pk;
      acc_pk.account = d_bytes(d_get(accs, key("address")));
      acc_pk.pub_key = d_bytes(d_get(accs, key("pub_key")));

      if (!acc_pk.account.data || !acc_pk.pub_key.data) return IN3_EINVAL;

      add_account_pub_key_to_utxo(utxo, &acc_pk);
    }
  }

  return IN3_OK;
}

static in3_ret_t btc_fill_utxo(btc_utxo_t* utxo, d_token_t* utxo_input) {
  if (!utxo || !utxo_input) return IN3_EINVAL;
  if (d_type(utxo_input) != T_OBJECT) return IN3_EINVAL;

  bytes_t  tx_hash  = d_bytes(d_get(utxo_input, key("tx_hash")));
  uint32_t tx_index = d_get_long(d_get(utxo_input, key("tx_index")), 0L);

  d_token_t* prevout_data   = d_get(utxo_input, key("tx_out"));
  uint64_t   value          = d_get_long(d_get(prevout_data, key("value")), 0L);
  bytes_t    locking_script = d_bytes(d_get(prevout_data, key("script")));

  d_token_t* utxo_args = d_get(utxo_input, key("args"));

  // Write the values we have
  btc_init_utxo(utxo);
  utxo->tx_hash            = tx_hash.data;
  utxo->tx_index           = tx_index;
  utxo->tx_out.value       = value;
  utxo->tx_out.script.data = locking_script;
  utxo->tx_out.script.type = btc_get_script_type(&locking_script);
  TRY_CATCH(handle_utxo_arg(utxo, utxo_args), btc_free_utxo(utxo))

  return IN3_OK;
}

// TODO: Currently we are adding all utxo_inputs to the list of selected_utxos. Implement an algorithm to select only the necessary utxos for the transaction, given the outputs.
in3_ret_t btc_prepare_utxos(in3_req_t* req, btc_tx_ctx_t* tx_ctx, btc_account_pub_key_t* default_account, d_token_t* utxo_inputs) {
  if (!tx_ctx || !utxo_inputs) return req_set_error(req, "ERROR: in btc_prepare_utxos: transaction context cannot be null", IN3_EINVAL);
  if (d_type(utxo_inputs) != T_ARRAY) return req_set_error(req, "ERROR: in btc_prepare_utxos: invalid utxo data format", IN3_EINVAL);

  uint32_t utxo_count = d_len(utxo_inputs);
  tx_ctx->utxo_count  = 0;
  tx_ctx->utxos       = _malloc(utxo_count * sizeof(btc_utxo_t));

  // Read and initialize each utxo we need for the transaction
  // TODO: Only add the necessary utxos to transaction. Choose them based on set of outputs
  for (uint32_t i = 0; i < utxo_count; i++) {
    btc_utxo_t utxo;
    d_token_t* utxo_input = d_get_at(utxo_inputs, i);
    btc_init_utxo(&utxo);
    TRY_CATCH(btc_fill_utxo(&utxo, utxo_input), btc_free_utxo(&utxo));
    btc_stype_t script_type = utxo.tx_out.script.type;
    if (script_type == BTC_UNKNOWN || script_type == BTC_NON_STANDARD || script_type == BTC_UNSUPPORTED) {
      return req_set_error(req, "ERROR: in btc_prepare_utxos: utxo script type is non standard or unsupported", IN3_ENOTSUP);
    }
    // finally, add utxo to context
    tx_ctx->utxos[i] = utxo;
    tx_ctx->utxo_count++;
  }

  // Now that all optional arguments were parsed, we fill the last remaining
  // fields into our utxo data
  for (uint32_t i = 0; i < tx_ctx->utxo_count; i++) {
    btc_utxo_t* utxo = &tx_ctx->utxos[i];
    btc_stype_t type = utxo->tx_out.script.type;

    if (type == BTC_P2SH || type == BTC_P2WSH) {
      // argument containing unhashed script should have been provided
      // otherwise it is impossible to obtain a signature
      if (!utxo->raw_script.data.len) {
        return req_set_error(req, "ERROR: in btc_prepare_utxos: utxo unhashed script not provided in P2SH or P2WSH transaction", IN3_ENOTSUP);
      }
      type = utxo->raw_script.type; // get the type of the unhashed script instead
    }
    else {
      utxo->raw_script = utxo->tx_out.script;
    }

    // how many signatures do we need to unlock the utxo?
    if (type == BTC_P2MS) {
      utxo->req_sigs = utxo->raw_script.data.data[1];
    }
    else {
      utxo->req_sigs = 1;
    }

    // Guarantee every utxo has at least one account<->pub_key pair assigned to it
    if (!utxo->accounts) {
      add_account_pub_key_to_utxo(utxo, default_account);
    }
  }

  return IN3_OK;
}

in3_ret_t btc_set_segwit(btc_tx_ctx_t* tx_ctx) {
  tx_ctx->tx.flag = 0;
  for (uint32_t i = 0; i < tx_ctx->utxo_count; i++) {
    if (tx_ctx->utxos[i].tx_out.script.data.data[0] < OP_PUSHDATA1) {
      tx_ctx->tx.flag = 1;
      break;
    }
  }
  return IN3_OK;
}
