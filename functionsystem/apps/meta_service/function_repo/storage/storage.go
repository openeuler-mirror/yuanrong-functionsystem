/*
 * Copyright (c) 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Package storage implements a persistence layer of repository, it deals with repository's metadata of
// function, alias, trigger, layer and related index tables
package storage

import (
	"context"

	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"
	"meta_service/function_repo/utils"

	"meta_service/common/engine"
	"meta_service/common/engine/etcd"
	"meta_service/common/logger/log"

	commonetcd "meta_service/common/etcd3"
)

var (
	db     *generatedKV
	metaDB *generatedKV
)

// InitStorage initiates storage module by specifying the etcd client.
func InitStorage(client *commonetcd.EtcdClient) error {
	eng := etcd.NewEtcdEngine(client, etcd.DefaultConfig)
	db = newKV(eng, utils.MetadataPrefix())
	return nil
}

// InitMetaStorage initiates storage module by specifying the meta etcd client.
func InitMetaStorage(client *commonetcd.EtcdClient) error {
	eng := etcd.NewEtcdEngine(client, etcd.DefaultConfig)
	metaDB = newKV(eng, utils.MetadataPrefix())
	return nil
}

// InitStorageByEng initiates storage module by specifying the engine
func InitStorageByEng(eng engine.Engine, prefix string) error {
	db = newKV(eng, prefix)
	return nil
}

// Put saves a key-value pair into storage
func Put(ctx context.Context, key string, value string) error {
	if db == nil {
		log.GetLogger().Errorf("uninitialized db")
		return errmsg.EtcdInternalError
	}
	return db.Put(ctx, key, value)
}

// Transaction -
type Transaction interface {
	Commit() error
	Cancel()
	GetCtx() server.Context
	GetTxn() *generatedTx
	Put(key string, value string)
	Delete(key string)
	DeleteRange(prefix string)
}

// Txn is used to utilize etcd transaction with specified etcd v3 client.
type Txn struct {
	txn *generatedTx
	c   server.Context
}

// NewTxn returns a new transaction object.
func NewTxn(c server.Context) *Txn {
	if db == nil {
		log.GetLogger().Errorf("uninitialized db")
		return nil
	}
	txn := db.BeginTx(c.Context())
	return &Txn{
		txn: txn,
		c:   c,
	}
}

// Commit commits the transaction.
func (tx *Txn) Commit() error {
	return tx.txn.Commit()
}

// Cancel cancels the timer that set in the context.
func (tx *Txn) Cancel() {
	tx.txn.Cancel()
}

// GetCtx returns the inner server.Context that creates the transaction.
func (tx *Txn) GetCtx() server.Context {
	return tx.c
}

// GetTxn returns the inner generatedTx.
func (tx *Txn) GetTxn() *generatedTx {
	return tx.txn
}

// Put saves a key-value pair into storage within a transaction
func (tx *Txn) Put(key string, value string) {
	tx.txn.Put(key, value)
}

// Delete removes a key-value pair from storage within a transaction
func (tx *Txn) Delete(key string) {
	tx.txn.Delete(key)
}

// DeleteRange removes key-value pairs from storage by key's prefix within a transaction
func (tx *Txn) DeleteRange(prefix string) {
	tx.txn.DeleteRange(prefix)
}

// MetaTxn is used to utilize etcd transaction with specified etcd v3 client.
type MetaTxn struct {
	txn *generatedTx
	c   server.Context
}

// NewMetaTxn returns a new transaction object for meta etcd.
func NewMetaTxn(c server.Context) *MetaTxn {
	if metaDB == nil {
		log.GetLogger().Errorf("uninitialized metaDB")
		return nil
	}
	txn := metaDB.BeginTx(c.Context())
	return &MetaTxn{
		txn: txn,
		c:   c,
	}
}

// Commit commits the transaction.
func (tx *MetaTxn) Commit() error {
	return tx.txn.Commit()
}

// Cancel cancels the timer that set in the context.
func (tx *MetaTxn) Cancel() {
	tx.txn.Cancel()
}

// GetCtx returns the inner server.Context that creates the transaction.
func (tx *MetaTxn) GetCtx() server.Context {
	return tx.c
}

// GetTxn returns the inner generatedTx.
func (tx *MetaTxn) GetTxn() *generatedTx {
	return tx.txn
}

// Put saves a key-value pair into storage within a transaction
func (tx *MetaTxn) Put(key string, value string) {
	tx.txn.Put(key, value)
}

// Delete removes a key-value pair from storage within a transaction
func (tx *MetaTxn) Delete(key string) {
	tx.txn.Delete(key)
}

// DeleteRange removes key-value pairs from storage by key's prefix within a transaction
func (tx *MetaTxn) DeleteRange(prefix string) {
	tx.txn.DeleteRange(prefix)
}
