use ffi::{CStr, CString};
use std::convert::TryInto;
use std::ffi;
use std::str;

use libc::{c_char, strlen};
use rustc_hex::FromHex;

use async_trait::async_trait;

use crate::error::{Error, In3Result};
use crate::signer;
use crate::traits::{Client as ClientTrait, Signer, Storage, Transport};
use crate::transport::HttpTransport;

pub mod chain {
    pub type ChainId = u32;

    pub const MULTICHAIN: u32 = 0x0;
    pub const MAINNET: u32 = 0x01;
    pub const KOVAN: u32 = 0x2a;
    pub const TOBALABA: u32 = 0x44d;
    pub const GOERLI: u32 = 0x5;
    pub const EVAN: u32 = 0x4b1;
    pub const IPFS: u32 = 0x7d0;
    pub const BTC: u32 = 0x99;
    pub const LOCAL: u32 = 0xffff;
}

struct Ctx {
    ptr: *mut in3_sys::in3_ctx_t,
    #[allow(dead_code)]
    config: ffi::CString,
}

impl Ctx {
    fn new(in3: &mut Client, config_str: &str) -> Ctx {
        let config = ffi::CString::new(config_str).expect("CString::new failed");
        let ptr: *mut in3_sys::in3_ctx_t;
        unsafe {
            ptr = in3_sys::ctx_new(in3.ptr, config.as_ptr());
        }
        Ctx { ptr, config }
    }

    unsafe fn signc(&mut self, data: *const c_char, len: usize) -> *mut u8 {
        let pk = (*(*(*self.ptr).client).signer).wallet as *mut u8;
        signer::signc(pk, data, len)
    }

    unsafe fn sign(&mut self, msg: &str) -> *const c_char {
        let cptr = (*self.ptr).client;
        let client = cptr as *mut in3_sys::in3_t;
        let c = (*client).internal as *mut Client;
        let signer = &mut (*c).signer;
        let no_signer = signer.is_none();
        if no_signer {
            let data_hex = msg.from_hex().unwrap();
            let c_data = data_hex.as_ptr() as *const c_char;
            let data_sig: *mut u8 = self.signc(c_data, data_hex.len());
            let c_sig = data_sig as *const c_char;
            return c_sig;
        } else if let Some(signer) = &mut (*c).signer {
            let sig = signer.sign(msg);
            return sig;
        }
        std::ptr::null_mut()
    }

    async unsafe fn execute(&mut self) -> In3Result<String> {
        let mut last_waiting: *mut in3_sys::in3_ctx_t = std::ptr::null_mut();
        let mut p: *mut in3_sys::in3_ctx_t;
        p = self.ptr;
        match in3_sys::in3_ctx_execute(self.ptr) {
            in3_sys::in3_ret_t::IN3_EIGNORE => {
                while p != std::ptr::null_mut() {
                    let p_req = (*p).required;
                    if p_req != std::ptr::null_mut()
                        && (*p_req).verification_state == in3_sys::in3_ret_t::IN3_EIGNORE
                    {
                        last_waiting = p;
                    }
                    p = (*last_waiting).required;
                }
                if last_waiting == std::ptr::null_mut() {
                    return Err("Cound not find the last waiting context".into());
                } else {
                    in3_sys::ctx_handle_failable(last_waiting);
                    return Err(Error::TryAgain);
                }
            }
            in3_sys::in3_ret_t::IN3_WAITING => {
                while p != std::ptr::null_mut() {
                    if (*p).raw_response == std::ptr::null_mut()
                        && in3_sys::in3_ctx_state(p) == in3_sys::state::CTX_WAITING_FOR_RESPONSE
                    {
                        last_waiting = p;
                    }
                    p = (*p).required;
                }
                if last_waiting == std::ptr::null_mut() {
                    return Err("Cound not find the last waiting context".into());
                }
            }
            in3_sys::in3_ret_t::IN3_OK => {
                let result = (*(*self.ptr).response_context).c;
                let data = ffi::CStr::from_ptr(result).to_str().unwrap();
                return Ok(data.into());
            }
            err => {
                return Err(err.into());
            }
        }

        if last_waiting != std::ptr::null_mut() {
            let req = in3_sys::in3_create_request(last_waiting);
            let req_type = (*last_waiting).type_;
            match req_type {
                in3_sys::ctx_type::CT_SIGN => {
                    let ite_ = (*last_waiting).requests.offset(0);
                    let item_ = (*(*ite_)).data as *const c_char;
                    let slice = CStr::from_ptr(item_).to_str().unwrap();
                    let request: serde_json::Value = serde_json::from_str(slice).unwrap();
                    let data_str = &request["params"][0].as_str().unwrap()[2..];
                    let res_str = self.sign(data_str);
                    in3_sys::in3_req_add_response(req, 0.try_into().unwrap(), false, res_str, 65);
                }
                in3_sys::ctx_type::CT_RPC => {
                    let payload = ffi::CStr::from_ptr((*req).payload).to_str().unwrap();
                    let urls_len = (*req).urls_len;
                    let mut urls = Vec::new();
                    for i in 0..urls_len as usize {
                        let url = ffi::CStr::from_ptr(*(*req).urls.add(i)).to_str().unwrap();
                        urls.push(url);
                    }

                    let responses: Vec<Result<String, String>> = {
                        let transport = {
                            let c = (*(*last_waiting).client).internal as *mut Client;
                            &mut (*c).transport
                        };
                        transport.fetch(payload, &urls).await
                    };
                    for (i, resp) in responses.iter().enumerate() {
                        match resp {
                            Err(err) => {
                                let err_str = ffi::CString::new(err.to_string()).unwrap();
                                in3_sys::in3_req_add_response(
                                    req,
                                    i.try_into().unwrap(),
                                    true,
                                    err_str.as_ptr(),
                                    -1i32,
                                );
                            }
                            Ok(res) => {
                                let res_str = ffi::CString::new(res.to_string()).unwrap();
                                in3_sys::in3_req_add_response(
                                    req,
                                    i.try_into().unwrap(),
                                    false,
                                    res_str.as_ptr(),
                                    -1i32,
                                );
                            }
                        }
                    }
                    let res = *(*req).results.offset(0);
                    let mut err = Error::TryAgain;
                    if res.result.len == 0 {
                        let error = (*(*req).results.offset(0)).error;
                        err = ffi::CStr::from_ptr(error.data).to_str().unwrap().into();
                    }
                    in3_sys::request_free(req, last_waiting, false);
                    return Err(err.into());
                }
            }
        }
        return Err(Error::TryAgain);
    }

    #[cfg(feature = "blocking")]
    fn send(&mut self) -> In3Result<()> {
        unsafe {
            let ret = in3_sys::in3_send_ctx(self.ptr);
            match ret {
                in3_sys::in3_ret_t::IN3_OK => Ok(()),
                _ => Err(ret.into()),
            }
        }
    }
}

#[async_trait(? Send)]
impl ClientTrait for Client {
    fn configure(&mut self, config: &str) -> Result<(), String> {
        unsafe {
            let config_c = ffi::CString::new(config).expect("CString::new failed");
            let err = in3_sys::in3_configure(self.ptr, config_c.as_ptr());
            if err.as_ref().is_some() {
                return Err(ffi::CStr::from_ptr(err).to_str().unwrap().to_string());
            }
        }
        Ok(())
    }

    fn set_transport(&mut self, transport: Box<dyn Transport>) {
        self.transport = transport;
    }

    fn set_signer(&mut self, signer: Box<dyn Signer>) {
        self.signer = Some(signer);
    }

    fn set_storage(&mut self, storage: Box<dyn Storage>) {
        let no_storage = self.storage.is_none();
        self.storage = Some(storage);
        if no_storage {
            unsafe {
                (*self.ptr).cache = in3_sys::in3_set_storage_handler(
                    self.ptr,
                    Some(Client::in3_rust_storage_get),
                    Some(Client::in3_rust_storage_set),
                    Some(Client::in3_rust_storage_clear),
                    self.ptr as *mut libc::c_void,
                );
            }
        }
    }

    async fn rpc(&mut self, call: &str) -> In3Result<String> {
        let mut ctx = Ctx::new(self, call);
        loop {
            let res = unsafe { ctx.execute().await };
            if res != Err(Error::TryAgain) {
                return res;
            }
        }
    }

    #[cfg(feature = "blocking")]
    fn rpc_blocking(&mut self, call: &str) -> In3Result<String> {
        let mut null: *mut i8 = std::ptr::null_mut();
        let res: *mut *mut i8 = &mut null;
        let err: *mut *mut i8 = &mut null;
        let req_str = ffi::CString::new(call).unwrap();
        unsafe {
            let ret = in3_sys::in3_client_rpc_raw(self.ptr, req_str.as_ptr(), res, err);
            match ret {
                in3_sys::in3_ret_t::IN3_OK => {
                    Ok(ffi::CStr::from_ptr(*res).to_str().unwrap().to_string())
                }
                _ => Err(Error::CustomError(
                    ffi::CStr::from_ptr(*err).to_str().unwrap().to_string(),
                )),
            }
        }
    }

    fn set_pk_signer(&mut self, data: &str) {
        unsafe {
            let pk_ = self.hex_to_bytes(data);
            in3_sys::eth_set_pk_signer(self.ptr, pk_);
        }
    }
}

impl Drop for Ctx {
    fn drop(&mut self) {
        unsafe {
            in3_sys::ctx_free(self.ptr);
        }
    }
}

pub struct Client {
    ptr: *mut in3_sys::in3_t,
    transport: Box<dyn Transport>,
    signer: Option<Box<dyn Signer>>,
    storage: Option<Box<dyn Storage>>,
}

impl Client {
    pub fn new(chain_id: chain::ChainId) -> Box<Client> {
        unsafe {
            let mut c = Box::new(Client {
                ptr: in3_sys::in3_for_chain_auto_init(chain_id),
                transport: Box::new(HttpTransport {}),
                signer: None,
                storage: None,
            });
            let c_ptr: *mut ffi::c_void = &mut *c as *mut _ as *mut ffi::c_void;
            (*c.ptr).internal = c_ptr;
            #[cfg(feature = "blocking")]
            {
                (*c.ptr).transport = Some(Client::in3_rust_transport);
            }
            c
        }
    }
    unsafe extern "C" fn in3_rust_storage_get(
        cptr: *mut libc::c_void,
        key: *const libc::c_char,
    ) -> *mut in3_sys::bytes_t {
        let key = ffi::CStr::from_ptr(key).to_str().unwrap();
        let client = cptr as *mut in3_sys::in3_t;
        let c = (*client).internal as *mut Client;
        if let Some(storage) = &(*c).storage {
            if let Some(val) = storage.get(key) {
                return in3_sys::b_new(val.as_ptr(), val.len() as u32);
            }
        }
        std::ptr::null_mut()
    }

    unsafe extern "C" fn in3_rust_storage_set(
        cptr: *mut libc::c_void,
        key: *const libc::c_char,
        value: *mut in3_sys::bytes_t,
    ) {
        let key = ffi::CStr::from_ptr(key).to_str().unwrap();
        let value = std::slice::from_raw_parts_mut((*value).data, (*value).len as usize);
        let client = cptr as *mut in3_sys::in3_t;
        let c = (*client).internal as *mut Client;
        if let Some(storage) = &mut (*c).storage {
            storage.set(key, value);
        }
    }

    unsafe extern "C" fn in3_rust_storage_clear(cptr: *mut libc::c_void) {
        let client = cptr as *mut in3_sys::in3_t;
        let c = (*client).internal as *mut Client;
        if let Some(storage) = &mut (*c).storage {
            storage.clear();
        }
    }

    #[cfg(feature = "blocking")]
    extern "C" fn in3_rust_transport(
        request: *mut in3_sys::in3_request_t,
    ) -> in3_sys::in3_ret_t::Type {
        // internally calls the rust transport impl, i.e. Client.transport
        let mut urls = Vec::new();

        unsafe {
            let payload = ffi::CStr::from_ptr((*request).payload).to_str().unwrap();
            let urls_len = (*request).urls_len;
            for i in 0..urls_len as usize {
                let url = ffi::CStr::from_ptr(*(*request).urls.add(i))
                    .to_str()
                    .unwrap();
                urls.push(url);
            }

            let c = (*(*request).in3).internal as *mut Client;
            let responses: Vec<Result<String, String>> =
                (*c).transport.fetch_blocking(payload, &urls);

            let mut any_err = false;
            for (i, resp) in responses.iter().enumerate() {
                match resp {
                    Err(err) => {
                        any_err = true;
                        let err_str = ffi::CString::new(err.to_string()).unwrap();
                        in3_sys::sb_add_chars(
                            &mut (*(*request).results.add(i)).error,
                            err_str.as_ptr(),
                        );
                    }
                    Ok(res) => {
                        let res_str = ffi::CString::new(res.to_string()).unwrap();
                        in3_sys::sb_add_chars(
                            &mut (*(*request).results.add(i)).result,
                            res_str.as_ptr(),
                        );
                    }
                }
            }

            if urls_len as usize != responses.len() || any_err {
                return in3_sys::in3_ret_t::IN3_ETRANS;
            }
        }

        in3_sys::in3_ret_t::IN3_OK
    }
}

impl Drop for Client {
    fn drop(&mut self) {
        unsafe {
            in3_sys::in3_free(self.ptr);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_in3_config() {
        let mut in3 = Client::new(chain::MAINNET);
        let c = in3.configure(r#"{"autoUpdateList":false}"#);
        assert_eq!(c.is_err(), false);
    }
}
