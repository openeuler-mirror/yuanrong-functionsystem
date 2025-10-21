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

// Package colorprint Print with color
package colorprint

// ColorType Font color setting structure
type ColorType struct {
	FontColor       int
	FontLight       bool
	BackgroundColor int
	BackgroundLight bool
}

// StringColorInfo Keyword color
type StringColorInfo struct {
	Str   string
	Color ColorType
}

// PrintStringConfig Keywords printing structure
type PrintStringConfig struct {
	ColorConfig map[string]ColorType
}

var defaultColorConfig = map[string]ColorType{
	"success":   {FontColor: Green},
	"succeed":   {FontColor: Green},
	"succeeded": {FontColor: Green},
	"Succeeded": {FontColor: Green},
	"failed":    {FontColor: Red},
	"fail":      {FontColor: Red},
}

// Colorless Default Color
var Colorless = ColorType{
	FontColor:       None,
	FontLight:       false,
	BackgroundColor: None,
	BackgroundLight: false,
}

// SuccessColor Success Statement Color
var SuccessColor = ColorType{
	FontColor:       Green,
	FontLight:       false,
	BackgroundColor: None,
	BackgroundLight: false,
}

// ErrorColor Error Statement Color
var ErrorColor = ColorType{
	FontColor:       Red,
	FontLight:       false,
	BackgroundColor: None,
	BackgroundLight: false,
}

// InteractiveColor Interactive Statement Color
var InteractiveColor = ColorType{
	FontColor:       Yellow,
	FontLight:       true,
	BackgroundColor: None,
	BackgroundLight: false,
}

// KeywordColor Keyword Statement Color
var KeywordColor = ColorType{
	FontColor:       Cyan,
	FontLight:       false,
	BackgroundColor: None,
	BackgroundLight: false,
}
