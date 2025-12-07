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

package utils

import (
	"net/http"
	"strconv"

	"github.com/asaskevich/govalidator/v11"

	"meta_service/common/logger/log"
	"meta_service/common/snerror"
	"meta_service/function_repo/config"
	"meta_service/function_repo/errmsg"
	"meta_service/function_repo/server"
)

const (
	minPageIndex = 1
	maxPageSize  = 1000
	minPageSize  = 1
)

// Validate -
func Validate(v interface{}) error {
	if _, err := govalidator.ValidateStruct(v); err != nil {
		log.GetLogger().Errorf("failed to validate: %s", err.Error())
		return errmsg.NewParamError("%s", err.Error())
	}
	configs := config.RepoCfg
	if vv, ok := v.(interface {
		Validate(configs config.Configs) error
	}); ok {
		if err := vv.Validate(*configs); err != nil {
			log.GetLogger().Errorf("failed to validate: %s", err.Error())
			_, ok := err.(snerror.SNError)
			if ok {
				return err
			}
			return errmsg.NewParamError("%s", err.Error())
		}
	}
	return nil
}

// ShouldBindQuery -
func ShouldBindQuery(c server.Context, v interface{}) error {
	if err := c.Gin().ShouldBindQuery(v); err != nil {
		log.GetLogger().Errorf("failed to bind query: %s", err.Error())
		return errmsg.NewParamError("failed to bind query: %s", err.Error())
	}
	return Validate(v)
}

// QueryPaging -
func QueryPaging(c server.Context) (pageIndex, pageSize int, err error) {
	if idxstr := c.Gin().Query("pageIndex"); idxstr != "" {
		pageIndex, err = strconv.Atoi(idxstr)
		if err != nil {
			return
		}
		if pageIndex < minPageIndex {
			log.GetLogger().Errorf("pageIndex %d is out of range", pageIndex)
			err = errmsg.NewParamError("pageIndex is out of range")
			return
		}
	}

	if sizestr := c.Gin().Query("pageSize"); sizestr != "" {
		pageSize, err = strconv.Atoi(sizestr)
		if err != nil {
			return
		}
		if pageSize < minPageSize || pageSize > maxPageSize {
			log.GetLogger().Errorf("pageSize %d is out of range", pageSize)
			err = errmsg.NewParamError("pageSize is out of range")
			return
		}
	}
	return
}

// TransToSnError -
func TransToSnError(err error) snerror.SNError {
	e, ok := err.(snerror.SNError)
	if !ok {
		e = errmsg.NewParamError(err.Error())
	}
	return e
}

// BadRequest -
func BadRequest(c server.Context, err error) {
	e := TransToSnError(err)
	log.GetLogger().Errorf("request resource: %s, response with error code: %d, message: %s",
		c.ResourceInfo(), e.Code(), e.Error())
	c.Gin().Data(http.StatusInternalServerError, "", snerror.Marshal(e))
}

// WriteResponse -
func WriteResponse(c server.Context, v interface{}, err error) {
	if err != nil {
		BadRequest(c, err)
	} else {
		c.Gin().JSON(http.StatusOK, successJSONBody{
			Result: v,
		})
	}
}

// WriteSnErrorResponseWithJson -
func WriteSnErrorResponseWithJson(c server.Context, code int, err snerror.SNError) {
	log.GetLogger().Errorf("request resource: %s, response with http code: %d, message: %s",
		c.ResourceInfo(), code, err.Error())
	c.Gin().Data(code, "application/json; charset=utf-8", snerror.Marshal(err))
}

// WriteResponseWithMsg -
func WriteResponseWithMsg(c server.Context, v interface{}, err error) {
	if err == nil {
		c.Gin().JSON(http.StatusOK, successJSONBody{
			Result: v,
		})
		return
	}
	e := errmsg.NewParamError(err.Error())
	c.Gin().JSON(http.StatusOK, successJSONBody{
		Code:    e.Code(),
		Message: e.Error(),
		Result:  v,
	})

}

// successJSONBody structure returned when the processing is successful.
type successJSONBody struct {
	Code    int         `json:"code"`
	Message string      `json:"message"`
	Result  interface{} `json:"result"`
}
