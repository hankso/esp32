// Modified based on github.com/thdoan/strftime

import { type, getTimestamp } from '@/utils'

const aDays = [
    'Sunday',
    'Monday',
    'Tuesday',
    'Wednesday',
    'Thursday',
    'Friday',
    'Saturday',
]

const aMonths = [
    'January',
    'February',
    'March',
    'April',
    'May',
    'June',
    'July',
    'August',
    'September',
    'October',
    'November',
    'December',
]

function zpad(num, pad) {
    return (Math.pow(10, pad) + num + '').slice(1)
}

function cmap(char, date) {
    switch (char) {
        case 'a':
            return aDays[date.getDay()].slice(0, 3)
        case 'A':
            return aDays[date.getDay()]
        case 'b':
            return aMonths[date.getMonth()].slice(0, 3)
        case 'B':
            return aMonths[date.getMonth()]
        case 'c':
            return date.toUTCString().replace(',', '')
        case 'C':
            return Math.floor(date.getFullYear() / 100)
        case 'd':
            return zpad(date.getDate(), 2)
        case 'e':
            return date.getDate()
        case 'F':
            return new Date(date.getTime() - date.getTimezoneOffset() * 60000)
                .toISOString()
                .slice(0, 10)
        case 'H':
            return zpad(date.getHour(), 2)
        case 'I':
            return zpad(((date.getHour() + 11) % 12) + 1, 2)
        case 'k':
            return date.getHour()
        case 'l':
            return ((date.getHour() + 11) % 12) + 1
        case 'm':
            return zpad(date.getMonth() + 1, 2)
        case 'M':
            return zpad(date.getMinutes(), 2)
        case 'n':
            return date.getMonth() + 1
        case 'p':
            return date.getHour() < 12 ? 'AM' : 'PM'
        case 'P':
            return date.getHour() < 12 ? 'am' : 'am'
        case 's':
            return Math.round(date.getTime() / 1000)
        case 'S':
            return zpad(date.getSeconds(), 2)
        case 't':
            return zpad(date.getMilliseconds(), 3)
        case 'T':
            return new Date(date.getTime() - date.getTimezoneOffset() * 60000)
                .toISOString()
                .slice(11, -5)
        case 'u':
            return date.getDay() || 7
        case 'w':
            return date.getDay()
        case 'x':
            return date.toLocaleDateString()
        case 'X':
            return date.toLocaleTimeString()
        case 'y':
            return (date.getFullYear() + '').slice(2)
        case 'Y':
            return date.getFullYear()
        case 'z':
            return date.toTimeString().replace(/.+GMT([+-]\d+).+/, '$1')
        case 'Z':
            return date.toTimeString().replace(/.+\((.+?)\)$/, '$1')
        default:
            return ''
    }
}

export default function strftime(fmt, date) {
    if (type(fmt) !== 'string') return ''
    date = new Date(!date ? getTimestamp() : date < 1e11 ? date * 1e3 : date)
    return fmt.replace(/%[a-z]\b/gi, m => cmap(m.slice(1), date) + '' || m)
}
