/*
 * Copyright (c) 2016 Jari Saukkonen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "PowerConsumptionLog.h"

PowerConsumptionLog::PowerConsumptionLog() :
    m_pos(0),
    m_windowPos(0),
    m_overloadThreshold(5000) {
    memset(&m_data[0], 0, 16);
    memset(&m_window[0], 0, 16);
}

void PowerConsumptionLog::measure(unsigned int milliAmps) {
    m_window[m_windowPos++ & 0xF] = milliAmps;
}

void PowerConsumptionLog::appendCurrentMeasurement() {
    unsigned int avg = 0;
    for (int i = 1; i <= 10; i++) {
        avg += m_window[(m_windowPos-i) & 0xF];
    }
    m_data[m_pos++ & 0xF] = avg/10;
}

bool PowerConsumptionLog::isOverload(unsigned int threshold) const {
    for (int i = 1; i <= 3; i++) {
        if (m_data[(m_pos-i) & 0xF] < threshold) {
            return false;
        }
    }
    return true;
}

void PowerConsumptionLog::report(NMEASerial& serial) {
    String message = "POWER,";
    for (int i = 1; i <= 10; i++) {
        message += String(m_data[(m_pos-i) & 0xF]) + ",";
    }
    serial.print(message.substring(0, message.length()-1));
}
