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

import (
	"fmt"
	"io"
	"strconv"
	"strings"
)

func printColorString(w io.Writer, str string, color ColorType) {
	if color == Colorless {
		fmt.Fprint(w, str)
		return
	}
	result := getPrintString(str, color)
	fmt.Fprint(w, result)
}

func getColorCode(color ColorType) string {
	var fontCode int
	var backgroundCode int
	color = checkColor(color)
	var colorCode strings.Builder
	if color.FontColor == None && color.BackgroundColor == None {
		return "0"
	}
	if color.FontColor != None {
		fontCode = color.FontColor - 1 + frontMask
		if color.FontLight {
			fontCode += light
		}
		colorCode.WriteString(strconv.Itoa(fontCode))
	}

	if color.BackgroundColor != None {
		backgroundCode = color.BackgroundColor - 1 + backgroundMask
		if color.BackgroundLight {
			backgroundCode += light
		}
		if colorCode.Len() != 0 {
			colorCode.WriteString(";")
		}
		colorCode.WriteString(strconv.Itoa(backgroundCode))
	}
	return colorCode.String()
}

func checkColor(color ColorType) ColorType {
	// linux does not support gray
	if color.FontColor == Gray {
		color.FontColor = None
	}
	if color.BackgroundColor == Gray {
		color.BackgroundColor = None
	}
	return color
}

func getPrintString(str string, color ColorType) string {
	if color == Colorless {
		return str
	}
	var printString strings.Builder
	colorCode := getColorCode(color)
	printString.WriteString(fmt.Sprintf(controllerMask, colorCode))
	printString.WriteString(str)
	printString.WriteString(defaultConfig)
	return printString.String()
}
