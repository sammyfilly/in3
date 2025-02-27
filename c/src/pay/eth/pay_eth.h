/*******************************************************************************
 * This file is part of the Incubed project.
 * Sources: https://github.com/blockchainsllc/in3
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

/** @file
 * USN API.
 *
 * This header-file defines easy to use function, which are verifying USN-Messages.
 * */

#ifndef PAY_ETH_H
#define PAY_ETH_H
#ifdef __cplusplus
extern "C" {
#endif
#include "../../core/client/client.h"
#include "../../core/client/plugin.h"

typedef struct in3_pay_eth_node {
  address_t                address;
  uint32_t                 price;
  uint64_t                 payed;
  struct in3_pay_eth_node* next;
} in3_pay_eth_node_t;

typedef struct {
  uint64_t            bulk_size;
  uint64_t            max_price;
  uint64_t            nonce;
  uint64_t            gas_price;
  in3_pay_eth_node_t* nodes;
} in3_pay_eth_t;

/**
 * Eth payment implementation
 */
in3_ret_t in3_pay_eth(void* plugin_data, in3_plugin_act_t action, void* plugin_ctx);

/**
 * get access to internal plugin data if registered
 */
static inline in3_pay_eth_t* in3_pay_eth_data(in3_t* c) {
  return in3_plugin_get_data(c, in3_pay_eth);
}

/**
 * registers the Eth payment plugin
 */
in3_ret_t in3_register_pay_eth(in3_t* c);
#ifdef __cplusplus
}
#endif
#endif