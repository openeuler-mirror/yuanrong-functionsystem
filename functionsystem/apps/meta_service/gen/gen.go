/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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

package main

import (
	"bytes"
	"errors"
	"fmt"
	"go/ast"
	"go/parser"
	"go/printer"
	"go/token"
	"os"
	"path"
	"runtime"
	"strings"
	"text/template"
)

const (
	keyToken     = "Key"
	tupleToken   = "Tuple"
	valueToken   = "Value"
	requiredArgs = 4
)

// TupleModel use to storage type attribute
type TupleModel struct {
	Name  string
	Key   string
	Value string
}

func fatal(err error) {
	fmt.Println(err.Error())
	os.Exit(1)
}

func main() {
	if len(os.Args) != requiredArgs {
		fatal(fmt.Errorf("usage: %s -- [types.go] [kv.go]", os.Args[0]))
		return
	}

	ff, err := parser.ParseFile(token.NewFileSet(), os.Args[2], nil, 0)
	if err != nil {
		fatal(err)
		return
	}
	types, err := parseTypes(ff)
	if err != nil {
		fatal(err)
		return
	}

	f, err := os.Create(os.Args[3])
	if err != nil {
		fatal(err)
		return
	}
	defer f.Close()

	_, filename, _, ok := runtime.Caller(0)
	if !ok {
		fatal(errors.New("no caller info"))
	}
	tmpl, err := template.New("kv.go.tmpl").ParseFiles(path.Join(path.Dir(filename), "kv.go.tmpl"))
	if err != nil {
		fatal(err)
		return
	}

	err = tmpl.Execute(f, struct {
		Types []TupleModel
	}{
		Types: types,
	})
	if err != nil {
		fatal(err)
		return
	}
}

func parseTypes(f *ast.File) ([]TupleModel, error) {
	m := findTypes(f)
	res := make([]TupleModel, 0, len(m))
	for _, tupleModel := range m {
		if tupleModel.Value == "" || tupleModel.Key == "" {
			return nil, fmt.Errorf("%s is missing either Key or Value", tupleModel.Name)
		}
		res = append(res, tupleModel)
	}
	return res, nil
}

func findTypes(f *ast.File) []TupleModel {
	m := make([]TupleModel, 0)
	fset := token.NewFileSet()
	// Inspect the AST and print all identifiers and literals.
	ast.Inspect(f, func(n ast.Node) bool {
		switch x := n.(type) {
		case *ast.TypeSpec: // Gets Type assertions
			if strings.HasSuffix(x.Name.Name, tupleToken) {
				var key, value string
				v := x.Type.(*ast.StructType)
				for _, field := range v.Fields.List {
					for _, name := range field.Names {
						// get field.Type as string
						var typeNameBuf bytes.Buffer
						err := printer.Fprint(&typeNameBuf, fset, field.Type)
						if err != nil {
							fmt.Printf("failed printing %s", err)
						}
						if name.Name == keyToken {
							key = typeNameBuf.String()
						}
						if name.Name == valueToken {
							value = typeNameBuf.String()
						}
					}
				}
				m = append(m, TupleModel{
					Name:  strings.TrimSuffix(x.Name.Name, tupleToken),
					Key:   key,
					Value: value,
				})
			}
		}
		return true
	})
	return m
}
