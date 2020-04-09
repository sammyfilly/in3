use std::convert::TryInto;
use std::i64;

use hex::FromHex;
use serde::{Deserialize, Serialize};
use serde_json::{Result, Value};
use serde_json::json;

use crate::error::*;
use crate::eth1::U256;
use crate::in3::*;

#[derive(Serialize)]
pub struct RpcRequest<'a> {
    method: &'a str,
    params: serde_json::Value,
}

pub struct EthApi {
    client: Box<Client>,
}

impl EthApi {
    pub fn new(config_str: &str) -> EthApi {
        let mut client = Client::new(chain::MAINNET);
        let _ = client.configure(config_str);
        EthApi { client }
    }

    async fn send(&mut self, params: &str) -> In3Result<String> {
        self.client.send_request(params).await
    }

    pub async fn block_number(&mut self) -> In3Result<U256> {
        let resp = self.send(serde_json::to_string(&RpcRequest {
            method: "eth_blockNumber",
            params: json!([]),
        }).unwrap().as_str()).await?;
        let v: Value = serde_json::from_str(resp.as_str()).unwrap();
        let mut res = v[0]["result"].as_str().unwrap().trim_start_matches("0x");
        let mut u256 = U256([0; 32]);
        hex::decode_to_slice(format!("{:0>64}", res), &mut u256.0).expect("Decoding failed");
        Ok(u256)
    }

    pub async fn getBalance(&mut self, address: String) -> String {
        let payload = json!({
            "method": "eth_getBalance",
            "params": [
                address,
                "latest"
            ]
        });
        let serialized = serde_json::to_string(&payload).unwrap();
        let response = self.send(&serialized).await;
        let v: Value = serde_json::from_str(&response.unwrap()).unwrap();
        let balance = v[0]["result"].as_str().unwrap();
        balance.to_string()
    }
}

#[cfg(test)]
mod tests {
    use async_std::task;

    use super::*;

    #[test]
    fn test_block_number() {
        let mut api = EthApi::new(r#"{"autoUpdateList":false,"nodes":{"0x1":{"needsUpdate":false}}}}"#);
        let num: u64 = task::block_on(api.block_number()).unwrap().try_into().unwrap();
        println!("{:?}", num);
        assert!(num > 9000000, "Block number is not correct");
    }

    #[test]
    fn test_get_balance() {
        let mut api = EthApi::new(r#"{"autoUpdateList":false,"nodes":{"0x1":{"needsUpdate":false}}}}"#);
        //execute the call to the api on task::block_on
        let num = task::block_on(
            api.getBalance("0xc94770007dda54cF92009BFF0dE90c06F603a09f".to_string()),
        );
        assert!(num != "", "Balance is not correct");
    }
}
