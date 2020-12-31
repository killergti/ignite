/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ignite/impl/thin/writable_key.h>

#include "impl/response_status.h"
#include "impl/message.h"
#include "impl/cache/cache_client_impl.h"
#include "impl/transactions/transactions_impl.h"

using namespace ignite::impl::thin::transactions;
using namespace ignite::common::concurrent;

namespace ignite
{
    namespace impl
    {
        namespace thin
        {
            namespace cache
            {
                typedef SharedPointer<TransactionImpl> SP_TransactionImpl;

                CacheClientImpl::CacheClientImpl(
                        const SP_DataRouter& router,
                        const transactions::SP_TransactionsImpl& tx,
                        const std::string& name,
                        int32_t id) :
                    router(router),
                    tx(tx),
                    name(name),
                    id(id),
                    binary(false)
                {
                    // No-op.
                }

                CacheClientImpl::~CacheClientImpl()
                {
                    // No-op.
                }

                template<typename ReqT, typename RspT>
                void CacheClientImpl::SyncCacheKeyMessage(const WritableKey& key, const ReqT& req, RspT& rsp)
                {
                    DataRouter& router0 = *router.Get();

                    if (router0.IsPartitionAwarenessEnabled())
                    {
                        affinity::SP_AffinityAssignment affinityInfo = router0.GetAffinityAssignment(id);

                        if (!affinityInfo.IsValid())
                        {
                            router0.RefreshAffinityMapping(id);

                            affinityInfo = router0.GetAffinityAssignment(id);
                        }

                        if (!affinityInfo.IsValid() || affinityInfo.Get()->GetPartitionsNum() == 0)
                        {
                            router0.SyncMessage(req, rsp);
                        }
                        else
                        {
                            const Guid& guid = affinityInfo.Get()->GetNodeGuid(key);

                            router0.SyncMessage(req, rsp, guid);
                        }
                    }
                    else
                    {
                        router0.SyncMessage(req, rsp);
                    }

                    if (rsp.GetStatus() != ResponseStatus::SUCCESS)
                        throw IgniteError(IgniteError::IGNITE_ERR_CACHE, rsp.GetError().c_str());
                }

                template<typename ReqT, typename RspT>
                SP_DataChannel CacheClientImpl::SyncMessage(const ReqT& req, RspT& rsp)
                {
                    SP_DataChannel channel = router.Get()->SyncMessage(req, rsp);

                    if (rsp.GetStatus() != ResponseStatus::SUCCESS)
                        throw IgniteError(IgniteError::IGNITE_ERR_CACHE, rsp.GetError().c_str());

                    return channel;
                }

                template<typename ReqT>
                void CacheClientImpl::checkTransactional(ReqT& req)
                {
                    SP_TransactionImpl activeTx = tx.Get()->GetCurrent();

                    bool isUnderTx = activeTx.IsValid();

                    int32_t txId = isUnderTx ? activeTx.Get()->TxId() : 0;

                    req.activeTx(isUnderTx, txId);
                }

                void CacheClientImpl::Put(const WritableKey& key, const Writable& value)
                {
                    Cache2ValueRequest<RequestType::CACHE_PUT> req(id, binary, key, value);

                    checkTransactional(req);

                    Response rsp;

                    SyncCacheKeyMessage(key, req, rsp);
                }

                void CacheClientImpl::Get(const WritableKey& key, Readable& value)
                {
                    CacheValueRequest<RequestType::CACHE_GET> req(id, binary, key);

                    checkTransactional(req);

                    CacheValueResponse rsp(value);

                    SyncCacheKeyMessage(key, req, rsp);
                }

                void CacheClientImpl::PutAll(const Writable & pairs)
                {
                    CacheValueRequest<RequestType::CACHE_PUT_ALL> req(id, binary, pairs);

                    checkTransactional(req);

                    Response rsp;

                    SyncMessage(req, rsp);
                }

                void CacheClientImpl::GetAll(const Writable& keys, Readable& pairs)
                {
                    CacheValueRequest<RequestType::CACHE_GET_ALL> req(id, binary, keys);

                    checkTransactional(req);

                    CacheValueResponse rsp(pairs);

                    SyncMessage(req, rsp);
                }

                bool CacheClientImpl::Replace(const WritableKey& key, const Writable& value)
                {
                    Cache2ValueRequest<RequestType::CACHE_REPLACE> req(id, binary, key, value);

                    checkTransactional(req);

                    BoolResponse rsp;

                    SyncCacheKeyMessage(key, req, rsp);

                    return rsp.GetValue();
                }

                bool CacheClientImpl::ContainsKey(const WritableKey& key)
                {
                    CacheValueRequest<RequestType::CACHE_CONTAINS_KEY> req(id, binary, key);

                    checkTransactional(req);

                    BoolResponse rsp;

                    SyncCacheKeyMessage(key, req, rsp);

                    return rsp.GetValue();
                }

                bool CacheClientImpl::ContainsKeys(const Writable& keys)
                {
                    CacheValueRequest<RequestType::CACHE_CONTAINS_KEYS> req(id, binary, keys);

                    checkTransactional(req);

                    BoolResponse rsp;

                    SyncMessage(req, rsp);

                    return rsp.GetValue();
                }

                int64_t CacheClientImpl::GetSize(int32_t peekModes)
                {
                    CacheGetSizeRequest req(id, binary, peekModes);

                    checkTransactional(req);

                    Int64Response rsp;

                    SyncMessage(req, rsp);

                    return rsp.GetValue();
                }

                bool CacheClientImpl::Remove(const WritableKey& key)
                {
                    CacheValueRequest<RequestType::CACHE_REMOVE_KEY> req(id, binary, key);

                    checkTransactional(req);

                    BoolResponse rsp;

                    SyncCacheKeyMessage(key, req, rsp);

                    return rsp.GetValue();
                }

                bool CacheClientImpl::Remove(const WritableKey& key, const Writable& val)
                {
                    Cache2ValueRequest<RequestType::CACHE_REMOVE_IF_EQUALS> req(id, binary, key, val);

                    checkTransactional(req);

                    BoolResponse rsp;

                    SyncCacheKeyMessage(key, req, rsp);

                    return rsp.GetValue();
                }

                void CacheClientImpl::RemoveAll(const Writable& keys)
                {
                    CacheValueRequest<RequestType::CACHE_REMOVE_KEYS> req(id, binary, keys);

                    checkTransactional(req);

                    Response rsp;

                    SyncMessage(req, rsp);
                }

                void CacheClientImpl::RemoveAll()
                {
                    CacheRequest<RequestType::CACHE_REMOVE_ALL> req(id, binary);

                    checkTransactional(req);

                    Response rsp;

                    SyncMessage(req, rsp);
                }

                void CacheClientImpl::Clear(const WritableKey& key)
                {
                    CacheValueRequest<RequestType::CACHE_CLEAR_KEY> req(id, binary, key);

                    checkTransactional(req);

                    Response rsp;

                    SyncCacheKeyMessage(key, req, rsp);
                }

                void CacheClientImpl::Clear()
                {
                    CacheRequest<RequestType::CACHE_CLEAR> req(id, binary);

                    checkTransactional(req);

                    Response rsp;

                    SyncMessage(req, rsp);
                }

                void CacheClientImpl::ClearAll(const Writable& keys)
                {
                    CacheValueRequest<RequestType::CACHE_CLEAR_KEYS> req(id, binary, keys);

                    checkTransactional(req);

                    Response rsp;

                    SyncMessage(req, rsp);
                }

                void CacheClientImpl::LocalPeek(const WritableKey& key, Readable& value)
                {
                    CacheValueRequest<RequestType::CACHE_LOCAL_PEEK> req(id, binary, key);

                    checkTransactional(req);

                    CacheValueResponse rsp(value);

                    SyncCacheKeyMessage(key, req, rsp);
                }

                bool CacheClientImpl::Replace(const WritableKey& key, const Writable& oldVal, const Writable& newVal)
                {
                    Cache3ValueRequest<RequestType::CACHE_REPLACE_IF_EQUALS> req(id, binary, key, oldVal, newVal);

                    checkTransactional(req);

                    BoolResponse rsp;

                    SyncCacheKeyMessage(key, req, rsp);

                    return rsp.GetValue();
                }

                void CacheClientImpl::GetAndPut(const WritableKey& key, const Writable& valIn, Readable& valOut)
                {
                    Cache2ValueRequest<RequestType::CACHE_GET_AND_PUT> req(id, binary, key, valIn);

                    checkTransactional(req);

                    CacheValueResponse rsp(valOut);

                    SyncCacheKeyMessage(key, req, rsp);
                }

                void CacheClientImpl::GetAndRemove(const WritableKey& key, Readable& valOut)
                {
                    CacheValueRequest<RequestType::CACHE_GET_AND_REMOVE> req(id, binary, key);

                    checkTransactional(req);

                    CacheValueResponse rsp(valOut);

                    SyncCacheKeyMessage(key, req, rsp);
                }

                void CacheClientImpl::GetAndReplace(const WritableKey& key, const Writable& valIn, Readable& valOut)
                {
                    Cache2ValueRequest<RequestType::CACHE_GET_AND_REPLACE> req(id, binary, key, valIn);

                    checkTransactional(req);

                    CacheValueResponse rsp(valOut);

                    SyncCacheKeyMessage(key, req, rsp);
                }

                bool CacheClientImpl::PutIfAbsent(const WritableKey& key, const Writable& val)
                {
                    Cache2ValueRequest<RequestType::CACHE_PUT_IF_ABSENT> req(id, binary, key, val);

                    checkTransactional(req);

                    BoolResponse rsp;

                    SyncCacheKeyMessage(key, req, rsp);

                    return rsp.GetValue();
                }

                void CacheClientImpl::GetAndPutIfAbsent(const WritableKey& key, const Writable& valIn, Readable& valOut)
                {
                    Cache2ValueRequest<RequestType::CACHE_GET_AND_PUT_IF_ABSENT> req(id, binary, key, valIn);

                    checkTransactional(req);

                    CacheValueResponse rsp(valOut);

                    SyncCacheKeyMessage(key, req, rsp);
                }

                query::SP_QueryFieldsCursorImpl CacheClientImpl::Query(
                    const ignite::thin::cache::query::SqlFieldsQuery &qry)
                {
                    SqlFieldsQueryRequest req(id, qry);
                    SqlFieldsQueryResponse rsp;

                    SP_DataChannel channel = SyncMessage(req, rsp);

                    query::SP_QueryFieldsCursorImpl cursorImpl(
                        new query::QueryFieldsCursorImpl(
                            rsp.GetCursorId(),
                            rsp.GetColumns(),
                            rsp.GetCursorPage(),
                            channel,
                            static_cast<int32_t>(qry.GetTimeout())));

                    return cursorImpl;
                }
            }
        }
    }
}

