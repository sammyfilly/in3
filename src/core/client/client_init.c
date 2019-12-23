/*******************************************************************************
 * This file is part of the Incubed project.
 * Sources: https://github.com/slockit/in3-c
 * 
 * Copyright (C) 2018-2019 slock.it GmbH, Blockchains LLC
 * 
 * 
 * COMMERCIAL LICENSE USAGE
 * 
 * Licensees holding a valid commercial license may use this file in accordance 
 * with the commercial license agreement provided with the Software or, alternatively, 
 * in accordance with the terms contained in a written agreement between you and 
 * slock.it GmbH/Blockchains LLC. For licensing terms and conditions or further 
 * information please contact slock.it at in3@slock.it.
 * 	
 * Alternatively, this file may be used under the AGPL license as follows:
 *    
 * AGPL LICENSE USAGE
 * 
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free Software 
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
 * PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
 * [Permissions of this strong copyleft license are conditioned on making available 
 * complete source code of licensed works and modifications, which include larger 
 * works using a licensed work, under the same license. Copyright and license notices 
 * must be preserved. Contributors provide an express grant of patent rights.]
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *******************************************************************************/

#include "../util/data.h"
#include "../util/log.h"
#include "../util/mem.h"
#include "cache.h"
#include "client.h"
#include "nodelist.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// set the defaults
static in3_transport_send     default_transport = NULL;
static in3_storage_handler_t* default_storage   = NULL;
static in3_signer_t*          default_signer    = NULL;

/**
 * defines a default transport which is used when creating a new client.
 */
void in3_set_default_transport(in3_transport_send transport) {
  default_transport = transport;
}

/**
 * defines a default storage handler which is used when creating a new client.
 */
void in3_set_default_storage(in3_storage_handler_t* cacheStorage) {
  default_storage = cacheStorage;
}
/**
 * defines a default signer which is used when creating a new client.
 */
void in3_set_default_signer(in3_signer_t* signer) {
  default_signer = signer;
}

static void initChain(in3_chain_t* chain, uint64_t chainId, char* contract, char* registry_id, uint8_t version, int boot_node_count, in3_chain_type_t type) {
  chain->chainId        = chainId;
  chain->initAddresses  = NULL;
  chain->lastBlock      = 0;
  chain->contract       = hex2byte_new_bytes(contract, 40);
  chain->needsUpdate    = chainId == ETH_CHAIN_ID_LOCAL ? 0 : 1;
  chain->nodeList       = _malloc(sizeof(in3_node_t) * boot_node_count);
  chain->nodeListLength = boot_node_count;
  chain->weights        = _malloc(sizeof(in3_node_weight_t) * boot_node_count);
  chain->type           = type;
  chain->version        = version;
  memset(chain->registry_id, 0, 32);
  if (version > 1) {
    int l = hex2byte_arr(registry_id, -1, chain->registry_id, 32);
    if (l < 32) {
      memmove(chain->registry_id + 32 - l, chain->registry_id, l);
      memset(chain->registry_id, 0, 32 - l);
    }
  }
}

static void initNode(in3_chain_t* chain, int node_index, char* address, char* url) {
  in3_node_t* node = chain->nodeList + node_index;
  node->address    = hex2byte_new_bytes(address, 40);
  node->index      = node_index;
  node->capacity   = 1;
  node->deposit    = 0;
  node->props      = chain->chainId == ETH_CHAIN_ID_LOCAL ? 0x0 : 0xFF;
  node->url        = _malloc(strlen(url) + 1);
  memcpy(node->url, url, strlen(url) + 1);

  in3_node_weight_t* weight   = chain->weights + node_index;
  weight->blacklistedUntil    = 0;
  weight->response_count      = 0;
  weight->total_response_time = 0;
  weight->weight              = 1;
}

static void in3_client_init(in3_t* c) {
  c->autoUpdateList     = 1;
  c->cacheStorage       = NULL;
  c->signer             = NULL;
  c->cacheTimeout       = 0;
  c->use_binary         = 0;
  c->use_http           = 0;
  c->includeCode        = 0;
  c->chainId            = ETH_CHAIN_ID_MAINNET; // mainnet
  c->key                = NULL;
  c->finality           = 0;
  c->max_attempts       = 3;
  c->maxBlockCache      = 0;
  c->maxCodeCache       = 0;
  c->minDeposit         = 0;
  c->nodeLimit          = 0;
  c->proof              = PROOF_STANDARD;
  c->replaceLatestBlock = 0;
  c->requestCount       = 1;
  c->chainsCount        = 5;
  c->chains             = _malloc(sizeof(in3_chain_t) * c->chainsCount);
  c->filters            = NULL;

  // mainnet
  initChain(c->chains, 0x01, "ac1b824795e1eb1f6e609fe0da9b9af8beaab60f", "23d5345c5c13180a8080bd5ddbe7cde64683755dcce6e734d95b7b573845facb", 2, 2, CHAIN_ETH);
  initNode(c->chains, 0, "45d45e6ff99e6c34a235d263965910298985fcfe", "https://in3-v2.slock.it/mainnet/nd-1");
  initNode(c->chains, 1, "1fe2e9bf29aa1938859af64c413361227d04059a", "https://in3-v2.slock.it/mainnet/nd-2");

#ifdef IN3_STAGING
  // kovan
  initChain(c->chains + 1, 0x2a, "0604014f2a5fdfafce3f2ec10c77c31d8e15ce6f", "d440f01322c8529892c204d3705ae871c514bafbb2f35907832a07322e0dc868", 2, 2, CHAIN_ETH);
  initNode(c->chains + 1, 0, "784bfa9eb182c3a02dbeb5285e3dba92d717e07a", "https://in3.stage.slock.it/kovan/nd-1");
  initNode(c->chains + 1, 1, "17cdf9ec6dcae05c5686265638647e54b14b41a2", "https://in3.stage.slock.it/kovan/nd-2");
#else
  // kovan
  initChain(c->chains + 1, 0x2a, "4c396dcf50ac396e5fdea18163251699b5fcca25", "92eb6ad5ed9068a24c1c85276cd7eb11eda1e8c50b17fbaffaf3e8396df4becf", 2, 2, CHAIN_ETH);
  initNode(c->chains + 1, 0, "45d45e6ff99e6c34a235d263965910298985fcfe", "https://in3-v2.slock.it/kovan/nd-1");
  initNode(c->chains + 1, 1, "1fe2e9bf29aa1938859af64c413361227d04059a", "https://in3-v2.slock.it/kovan/nd-2");
#endif

  // ipfs
  initChain(c->chains + 2, 0x7d0, "f0fb87f4757c77ea3416afe87f36acaa0496c7e9", NULL, 1, 2, CHAIN_IPFS);
  initNode(c->chains + 2, 0, "784bfa9eb182c3a02dbeb5285e3dba92d717e07a", "https://in3.slock.it/ipfs/nd-1");
  initNode(c->chains + 2, 1, "243D5BB48A47bEd0F6A89B61E4660540E856A33D", "https://in3.slock.it/ipfs/nd-5");

  // local
  initChain(c->chains + 3, 0xFFFF, "f0fb87f4757c77ea3416afe87f36acaa0496c7e9", NULL, 1, 1, CHAIN_ETH);
  initNode(c->chains + 3, 0, "784bfa9eb182c3a02dbeb5285e3dba92d717e07a", "http://localhost:8545");

#ifdef IN3_STAGING
  // goerli
  initChain(c->chains + 4, 0x05, "814fb2203f9848192307092337340dcf791a3fed", "0f687341e0823fa5288dc9edd8a00950b35cc7e481ad7eaccaf61e4e04a61e08", 2, 2, CHAIN_ETH);
  initNode(c->chains + 4, 0, "45d45e6ff99e6c34a235d263965910298985fcfe", "https://in3.stage.slock.it/goerli/nd-1");
  initNode(c->chains + 4, 1, "1fe2e9bf29aa1938859af64c413361227d04059a", "https://in3.stage.slock.it/goerli/nd-2");
#else
  // goerli
  initChain(c->chains + 4, 0x05, "5f51e413581dd76759e9eed51e63d14c8d1379c8", "67c02e5e272f9d6b4a33716614061dd298283f86351079ef903bf0d4410a44ea", 2, 2, CHAIN_ETH);
  initNode(c->chains + 4, 0, "45d45e6ff99e6c34a235d263965910298985fcfe", "https://in3-v2.slock.it/goerli/nd-1");
  initNode(c->chains + 4, 1, "1fe2e9bf29aa1938859af64c413361227d04059a", "https://in3-v2.slock.it/goerli/nd-2");
#endif
}

in3_chain_t* in3_find_chain(in3_t* c, uint64_t chain_id) {
  for (int i = 0; i < c->chainsCount; i++) {
    if (c->chains[i].chainId == chain_id) return &c->chains[i];
  }
  return NULL;
}

in3_ret_t in3_client_register_chain(in3_t* c, uint64_t chain_id, in3_chain_type_t type, address_t contract, bytes32_t registry_id, uint8_t version) {
  in3_chain_t* chain = in3_find_chain(c, chain_id);
  if (!chain) {
    c->chains = _realloc(c->chains, sizeof(in3_chain_t) * (c->chainsCount + 1), sizeof(in3_chain_t) * c->chainsCount);
    if (c->chains == NULL) return IN3_ENOMEM;
    chain                 = c->chains + c->chainsCount;
    chain->nodeList       = NULL;
    chain->nodeListLength = 0;
    chain->weights        = NULL;
    chain->initAddresses  = NULL;
    chain->lastBlock      = 0;
    c->chainsCount++;

  } else if (chain->contract)
    b_free(chain->contract);

  chain->chainId     = chain_id;
  chain->contract    = b_new((char*) contract, 20);
  chain->needsUpdate = 0;
  chain->type        = type;
  chain->version     = version;
  memcpy(chain->registry_id, registry_id, 32);
  return chain->contract ? IN3_OK : IN3_ENOMEM;
}

in3_ret_t in3_client_add_node(in3_t* c, uint64_t chain_id, char* url, in3_node_props_t props, address_t address) {
  in3_chain_t* chain = in3_find_chain(c, chain_id);
  if (!chain) return IN3_EFIND;
  in3_node_t* node       = NULL;
  int         node_index = chain->nodeListLength;
  for (int i = 0; i < chain->nodeListLength; i++) {
    if (memcmp(chain->nodeList[i].address->data, address, 20) == 0) {
      node       = chain->nodeList + i;
      node_index = i;
      break;
    }
  }
  if (!node) {
    chain->nodeList = chain->nodeList
                          ? _realloc(chain->nodeList, sizeof(in3_node_t) * (chain->nodeListLength + 1), sizeof(in3_node_t) * chain->nodeListLength)
                          : _calloc(chain->nodeListLength + 1, sizeof(in3_node_t));
    chain->weights = chain->weights
                         ? _realloc(chain->weights, sizeof(in3_node_weight_t) * (chain->nodeListLength + 1), sizeof(in3_node_weight_t) * chain->nodeListLength)
                         : _calloc(chain->nodeListLength + 1, sizeof(in3_node_weight_t));
    if (!chain->nodeList || !chain->weights) return IN3_ENOMEM;
    node           = chain->nodeList + chain->nodeListLength;
    node->address  = b_new((char*) address, 20);
    node->index    = chain->nodeListLength;
    node->capacity = 1;
    node->deposit  = 0;
    chain->nodeListLength++;
  } else
    _free(node->url);

  node->props = props;
  node->url   = _malloc(strlen(url) + 1);
  memcpy(node->url, url, strlen(url) + 1);

  in3_node_weight_t* weight   = chain->weights + node_index;
  weight->blacklistedUntil    = 0;
  weight->response_count      = 0;
  weight->total_response_time = 0;
  weight->weight              = 1;
  return IN3_OK;
}
in3_ret_t in3_client_remove_node(in3_t* c, uint64_t chain_id, address_t address) {
  in3_chain_t* chain = in3_find_chain(c, chain_id);
  if (!chain) return IN3_EFIND;
  int node_index = -1;
  for (int i = 0; i < chain->nodeListLength; i++) {
    if (memcmp(chain->nodeList[i].address->data, address, 20) == 0) {
      node_index = i;
      break;
    }
  }
  if (node_index == -1) return IN3_EFIND;
  if (chain->nodeList[node_index].url)
    _free(chain->nodeList[node_index].url);
  if (chain->nodeList[node_index].address)
    b_free(chain->nodeList[node_index].address);

  if (node_index < chain->nodeListLength - 1) {
    memmove(chain->nodeList + node_index, chain->nodeList + node_index + 1, sizeof(in3_node_t) * (chain->nodeListLength - 1 - node_index));
    memmove(chain->weights + node_index, chain->weights + node_index + 1, sizeof(in3_node_weight_t) * (chain->nodeListLength - 1 - node_index));
  }
  chain->nodeListLength--;
  if (!chain->nodeListLength) {
    _free(chain->nodeList);
    _free(chain->weights);
    chain->nodeList = NULL;
    chain->weights  = NULL;
  }
  return IN3_OK;
}
in3_ret_t in3_client_clear_nodes(in3_t* c, uint64_t chain_id) {
  in3_chain_t* chain = in3_find_chain(c, chain_id);
  if (!chain) return IN3_EFIND;
  in3_nodelist_clear(chain);
  chain->nodeList       = NULL;
  chain->weights        = NULL;
  chain->nodeListLength = 0;
  return IN3_OK;
}

/* frees the data */
void in3_free(in3_t* a) {
  int i;
  for (i = 0; i < a->chainsCount; i++) {
    in3_nodelist_clear(a->chains + i);
    b_free(a->chains[i].contract);
  }
  if (a->signer) _free(a->signer);
  _free(a->chains);

  if (a->filters != NULL) {
    in3_filter_t* f = NULL;
    for (size_t j = 0; j < a->filters->count; j++) {
      f = a->filters->array[j];
      if (f) f->release(f);
    }
    _free(a->filters->array);
    _free(a->filters);
  }
  _free(a);
}

in3_t* in3_new() {
  // initialize random with the timestamp as seed
  _srand(_time());

  // create new client
  in3_t* c = _calloc(1, sizeof(in3_t));
  in3_client_init(c);

  if (default_transport) c->transport = default_transport;
  if (default_storage) c->cacheStorage = default_storage;
  if (default_signer) c->signer = default_signer;

#ifndef TEST
  in3_log_set_quiet(1);
#endif
  return c;
}

static uint64_t chain_id(d_token_t* t) {
  if (d_type(t) == T_STRING) {
    char* c = d_string(t);
    if (!strcmp(c, "mainnet")) return 1;
    if (!strcmp(c, "kovan")) return 0x2a;
    return 1;
  }
  return d_long(t);
}

in3_ret_t in3_configure(in3_t* c, char* config) {
  d_track_keynames(1);
  d_clear_keynames();
  json_ctx_t* cnf = parse_json(config);
  d_track_keynames(0);
  in3_ret_t res = IN3_OK;

  if (!cnf || !cnf->result) return IN3_EINVAL;
  for (d_iterator_t iter = d_iter(cnf->result); iter.left; d_iter_next(&iter)) {
    if (iter.token->key == key("autoUpdateList"))
      c->autoUpdateList = d_int(iter.token) ? true : false;
    else if (iter.token->key == key("chainId"))
      c->chainId = chain_id(iter.token);
    else if (iter.token->key == key("signatureCount"))
      c->signatureCount = (uint8_t) d_int(iter.token);
    else if (iter.token->key == key("finality"))
      c->finality = (uint_fast16_t) d_int(iter.token);
    else if (iter.token->key == key("includeCode"))
      c->includeCode = d_int(iter.token) ? true : false;
    else if (iter.token->key == key("maxAttempts"))
      c->max_attempts = d_int(iter.token);
    else if (iter.token->key == key("keepIn3"))
      c->keep_in3 = d_int(iter.token);
    else if (iter.token->key == key("maxBlockCache"))
      c->maxBlockCache = d_int(iter.token);
    else if (iter.token->key == key("maxCodeCache"))
      c->maxCodeCache = d_int(iter.token);
    else if (iter.token->key == key("minDeposit"))
      c->minDeposit = d_long(iter.token);
    else if (iter.token->key == key("nodeLimit"))
      c->nodeLimit = (uint16_t) d_int(iter.token);
    else if (iter.token->key == key("proof"))
      c->proof = strcmp(d_string(iter.token), "full") == 0
                     ? PROOF_FULL
                     : (strcmp(d_string(iter.token), "standard") == 0 ? PROOF_STANDARD : PROOF_NONE);
    else if (iter.token->key == key("replaceLatestBlock"))
      c->replaceLatestBlock = (uint16_t) d_int(iter.token);
    else if (iter.token->key == key("requestCount"))
      c->requestCount = (uint8_t) d_int(iter.token);
    else if (iter.token->key == key("rpc")) {
      c->proof        = PROOF_NONE;
      c->chainId      = ETH_CHAIN_ID_LOCAL;
      c->requestCount = 1;
      in3_node_t* n   = in3_find_chain(c, c->chainId)->nodeList;
      if (n->url) _free(n);
      n->url = malloc(d_len(iter.token) + 1);
      if (!n->url) {
        res = IN3_ENOMEM;
        goto cleanup;
      }
      strcpy(n->url, d_string(iter.token));
    } else if (iter.token->key == key("servers") || iter.token->key == key("nodes"))
      for (d_iterator_t ct = d_iter(iter.token); ct.left; d_iter_next(&ct)) {
        // register chain
        uint64_t     chain_id = c_to_long(d_get_keystr(ct.token->key), -1);
        in3_chain_t* chain    = in3_find_chain(c, chain_id);
        if (!chain) {
          bytes_t* contract_t  = d_get_byteskl(ct.token, key("contract"), 20);
          bytes_t* registry_id = d_get_byteskl(ct.token, key("registryId"), 32);
          if (!contract_t || !registry_id) {
            res = IN3_EINVAL;
            goto cleanup;
          }
          if ((res = in3_client_register_chain(c, chain_id, CHAIN_ETH, contract_t->data, registry_id->data, 2)) != IN3_OK) goto cleanup;
          chain = in3_find_chain(c, chain_id);
          assert(chain != NULL);
        }

        // chain_props
        for (d_iterator_t cp = d_iter(ct.token); cp.left; d_iter_next(&cp)) {
          if (cp.token->key == key("contract"))
            memcpy(chain->contract->data, cp.token->data, cp.token->len);
          else if (cp.token->key == key("registryId")) {
            bytes_t data = d_to_bytes(cp.token);
            if (data.len != 32 || !data.data) {
              res = IN3_EINVAL;
              goto cleanup;
            } else
              memcpy(chain->registry_id, data.data, 32);
          } else if (cp.token->key == key("needsUpdate"))
            chain->needsUpdate = d_int(cp.token) ? true : false;
          else if (cp.token->key == key("nodeList")) {
            if (in3_client_clear_nodes(c, chain_id) < 0) goto cleanup;
            for (d_iterator_t n = d_iter(cp.token); n.left; d_iter_next(&n)) {
              if ((res = in3_client_add_node(c, chain_id, d_get_string(n.token, "url"),
                                             d_get_longkd(n.token, key("props"), 65535),
                                             d_get_byteskl(n.token, key("address"), 20)->data)) != IN3_OK) goto cleanup;
            }
          }
        }
      }
  }

cleanup:
  json_free(cnf);
  return res;
}
