/******************************************************************************
 *
 * Copyright (C) 2020 Marton Borzak <hello@martonborzak.com>
 *
 * This file is part of the YIO-Remote software project.
 *
 * YIO-Remote software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * YIO-Remote software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with YIO-Remote software. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *****************************************************************************/

#pragma once

#include <QVariant>

class LightFeatures {
 public:
    enum Features {
        SUPPORT_BRIGHTNESS  = 1,
        SUPPORT_COLOR_TEMP  = 2,
        SUPPORT_EFFECT      = 4,
        SUPPORT_FLASH       = 8,
        SUPPORT_COLOR       = 16,
        SUPPORT_TRANSITION  = 32,
        SUPPORT_WHITE_VALUE = 128
    };
};

class BlindFeatures {
 public:
    enum Features {
        SUPPORT_OPEN              = 1,
        SUPPORT_CLOSE             = 2,
        SUPPORT_SET_POSITION      = 4,
        SUPPORT_STOP              = 8,
        SUPPORT_OPEN_TILT         = 16,
        SUPPORT_CLOSE_TILT        = 32,
        SUPPORT_STOP_TILT         = 64,
        SUPPORT_SET_TILT_POSITION = 128
    };
};

class ClimateFeatures {
 public:
    enum Features {
        SUPPORT_TARGET_TEMPERATURE       = 1,
        SUPPORT_TARGET_TEMPERATURE_RANGE = 2,
        SUPPORT_TARGET_HUMIDITY          = 4,
        SUPPORT_FAN_MODE                 = 8,
        SUPPORT_PRESET_MODE              = 16,
        SUPPORT_SWING_MODE               = 32,
        SUPPORT_AUX_HEAT                 = 64
    };
};

class MediaPlayerFeatures {
 public:
    enum Features {
        SUPPORT_PAUSE             = 1,
        SUPPORT_SEEK              = 2,
        SUPPORT_VOLUME_SET        = 4,
        SUPPORT_VOLUME_MUTE       = 8,
        SUPPORT_PREVIOUS_TRACK    = 16,
        SUPPORT_NEXT_TRACK        = 32,
        SUPPORT_TURN_ON           = 128,
        SUPPORT_TURN_OFF          = 256,
        SUPPORT_PLAY_MEDIA        = 512,
        SUPPORT_VOLUME_STEP       = 1024,
        SUPPORT_SELECT_SOURCE     = 2048,
        SUPPORT_STOP              = 4096,
        SUPPORT_CLEAR_PLAYLIST    = 8192,
        SUPPORT_PLAY              = 16384,
        SUPPORT_SHUFFLE_SET       = 32768,
        SUPPORT_SELECT_SOUND_MODE = 65536
    };
};
