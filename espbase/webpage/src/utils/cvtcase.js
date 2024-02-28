// Modified on www.30secondsofcode.org/js/s/string-case-conversion

const cases = ['pascal', 'camel', 'kebab', 'snake', 'title', 'capital']

const words = new RegExp(
    /[A-Z]{2,}(?=[A-Z][a-z]+[0-9]*|\b)|[A-Z]?[a-z]+[0-9]*|[A-Z]|[0-9]+/g
)

function transform(fmt) {
    if (['camel', 'pascal'].includes(fmt))
        return x => x.slice(0, 1).toUpperCase() + x.slice(1).toLowerCase()
    if (['title'].includes(fmt))
        return x => x.slice(0, 1).toUpperCase() + x.slice(1)
    if (['snake', 'kebab'].includes(fmt)) return x => x.toLowerCase()
    return x => x
}

function delimiter(fmt) {
    switch (fmt) {
        case 'kebab':
            return '-'
        case 'snake':
            return '_'
        case 'title':
            return ' '
        case 'capital':
            return ' '
        default:
            return ''
    }
}

export default function cvtCase(str, fmt = 'camel') {
    if (!str) return ''
    fmt = (fmt + '').toLowerCase()
    str = (str + '').match(words).map(transform(fmt)).join(delimiter(fmt))
    switch (fmt) {
        case 'camel':
            return str.slice(0, 1).toLowerCase() + str.slice(1)
        case 'capital':
            return str.slice(0, 1).toUpperCase() + str.slice(1)
        default:
            return str
    }
}

export function cvtTest(str = 'mixed_str with space_underscore-and-hyphens') {
    cases.forEach(fmt => console.log(fmt + '\t', cvtCase(str, fmt)))
}
