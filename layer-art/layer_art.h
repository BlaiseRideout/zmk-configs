/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Behavior name the central invokes on the peripheral to relay its BLE profile
#define LAYER_ART_PROFILE_BEHAVIOR "bt_prof"

// On the peripheral: show the central's active BLE profile (index from 0)
void layer_art_set_profile(uint32_t index, bool connected);
