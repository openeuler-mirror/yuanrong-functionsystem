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

package codec

import (
	"errors"
	"fmt"
	"reflect"
	"strconv"
	"strings"
)

const (
	reflectSeperator = "/"
)

var (
	reflectEncodeIntFunc = func(field reflect.Value) string {
		if field.Int() != 0 {
			return formatInt(field.Int())
		}
		return ""
	}
	reflectEncodeUintFunc = func(field reflect.Value) string {
		if field.Uint() != 0 {
			return formatUint(field.Uint())
		}
		return ""
	}
	reflecteEncodeMap = map[reflect.Kind]func(field reflect.Value) string{
		reflect.String: func(field reflect.Value) string { return field.String() },
		reflect.Bool:   func(field reflect.Value) string { return formatBool(field.Bool()) },
		reflect.Int:    reflectEncodeIntFunc,
		reflect.Int64:  reflectEncodeIntFunc,
		reflect.Int32:  reflectEncodeIntFunc,
		reflect.Int16:  reflectEncodeIntFunc,
		reflect.Int8:   reflectEncodeIntFunc,
		reflect.Uint:   reflectEncodeUintFunc,
		reflect.Uint64: reflectEncodeUintFunc,
		reflect.Uint32: reflectEncodeUintFunc,
		reflect.Uint16: reflectEncodeUintFunc,
		reflect.Uint8:  reflectEncodeUintFunc,
	}
	reflectDecodeIntFunc = func(field *reflect.Value, s string) error {
		if s == "" {
			return nil
		}
		num, err := parseInt(s)
		if err != nil {
			return fmt.Errorf("failed to parse field %s to int, %s", field.Type().Name(), err.Error())
		}
		field.SetInt(num)
		return nil
	}
	reflectDecodeUintFunc = func(field *reflect.Value, s string) error {
		if s == "" {
			return nil
		}
		num, err := parseUint(s)
		if err != nil {
			return fmt.Errorf("failed to parse field %s to uint, %s", field.Type().Name(), err.Error())
		}
		field.SetUint(num)
		return nil
	}
	reflectDecodeMap = map[reflect.Kind]func(field *reflect.Value, s string) error{
		reflect.String: func(field *reflect.Value, s string) error {
			field.SetString(s)
			return nil
		},
		reflect.Bool: func(field *reflect.Value, s string) error {
			b, err := parseBool(s)
			if err != nil {
				return fmt.Errorf("failed to parse field %s to bool, %s", field.Type().Name(), err.Error())
			}
			field.SetBool(b)
			return nil
		},
		reflect.Int:    reflectDecodeIntFunc,
		reflect.Int64:  reflectDecodeIntFunc,
		reflect.Int32:  reflectDecodeIntFunc,
		reflect.Int16:  reflectDecodeIntFunc,
		reflect.Int8:   reflectDecodeIntFunc,
		reflect.Uint:   reflectDecodeUintFunc,
		reflect.Uint64: reflectDecodeUintFunc,
		reflect.Uint32: reflectDecodeUintFunc,
		reflect.Uint16: reflectDecodeUintFunc,
		reflect.Uint8:  reflectDecodeUintFunc,
	}
)

type reflectC struct {
	prefix    string
	hasSuffix bool
}

// NewReflectCodec encodes and decodes types to/from "/" separated strings.
func NewReflectCodec(prefix string, hasSuffix bool) Codec {
	return reflectC{prefix, hasSuffix}
}

// Encode implements Codec
func (c reflectC) Encode(v interface{}) (string, error) {
	val := reflect.ValueOf(v)
	tokens, err := c.encode(val)
	if err != nil {
		return "", err
	}

	var trim int
	for j := len(tokens) - 1; j >= 0; j-- {
		if tokens[j] == "" {
			trim++
		} else {
			break
		}
	}
	tokens = append([]string{c.prefix}, tokens...)
	if c.hasSuffix {
		return strings.Join(tokens[:len(tokens)-trim], reflectSeperator) + reflectSeperator, nil
	} else {
		return strings.Join(tokens[:len(tokens)-trim], reflectSeperator), nil
	}
}

func (c reflectC) encode(val reflect.Value) ([]string, error) {
	if val.Kind() != reflect.Struct {
		return nil, errors.New("interface should be a struct")
	}

	tokens := make([]string, 0, val.NumField())
	for i := 0; i < val.NumField(); i++ {
		field := val.Field(i)
		if field.Kind() == reflect.Struct {
			nested, err := c.encode(field)
			if err != nil {
				return nil, err
			}
			tokens = append(tokens, nested...)
			continue
		}
		fn, exist := reflecteEncodeMap[field.Kind()]
		if !exist {
			return nil, fmt.Errorf("unsupported type %v", field.Kind())
		}
		tokens = append(tokens, fn(field))
	}

	return tokens, nil
}

// Decode implements Codec
func (c reflectC) Decode(data string, v interface{}) error {
	if v == nil {
		return errors.New("interface should be a pointer of struct")
	}
	val := reflect.ValueOf(v)
	if val.Kind() != reflect.Ptr {
		return errors.New("interface should be a pointer of struct")
	}
	val = val.Elem()
	if val.Kind() != reflect.Struct {
		return errors.New("interface should be a pointer of struct")
	}
	if len(data) < len(c.prefix)+1 || !strings.HasPrefix(data, c.prefix+reflectSeperator) ||
		(c.hasSuffix && !strings.HasSuffix(data, reflectSeperator)) {
		return fmt.Errorf("data %s is invalid", data)
	}
	tokens := strings.Split(data[len(c.prefix):], reflectSeperator)
	if c.hasSuffix {
		tokens = tokens[1 : len(tokens)-1]
	} else {
		tokens = tokens[1:len(tokens)]
	}
	pos := 0
	return c.decode(val, &pos, tokens)
}

func (c reflectC) decode(val reflect.Value, pos *int, tokens []string) error {
	for i := 0; i < val.NumField(); i++ {
		field := val.Field(i)
		if field.Kind() == reflect.Struct {
			if err := c.decode(field, pos, tokens); err != nil {
				return err
			}
			continue
		}
		fn, exist := reflectDecodeMap[field.Kind()]
		if !exist {
			return fmt.Errorf("unsupported type %v", field.Kind())
		}
		if *pos >= len(tokens) {
			break
		}
		if err := fn(&field, tokens[*pos]); err != nil {
			return err
		}
		*pos++
	}
	return nil
}

func formatBool(b bool) string {
	if b {
		return "1"
	}
	return "0"
}

func parseBool(s string) (bool, error) {
	if s == "1" {
		return true, nil
	}
	if s == "0" {
		return false, nil
	}
	return false, fmt.Errorf("expect 0 or 1, got %s", s)
}

// Strategy: Prepend a number with its length formatted to rune so it becomes sortable.
const (
	base  = 'a'
	nbase = 10
)

func formatInt(i int64) string {
	s := strconv.FormatInt(i, nbase)
	if i >= 0 {
		return string(base+rune(len(s))) + s
	}
	return string(base-rune(len(s))) + s
}

func parseInt(s string) (int64, error) {
	if len(s) < 1 {
		return 0, errors.New("expect at least 1 rune")
	}
	s = s[1:]
	return strconv.ParseInt(s, nbase, 0)
}

func formatUint(i uint64) string {
	s := strconv.FormatUint(i, nbase)
	return string(base+rune(len(s))) + s
}

func parseUint(s string) (uint64, error) {
	if len(s) < 1 {
		return 0, errors.New("expect at least 1 rune")
	}
	s = s[1:]
	return strconv.ParseUint(s, nbase, 0)
}
