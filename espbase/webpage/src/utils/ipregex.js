// Modified based on github.com/sindresorhus/ip-regex

const v4reg = `
(?:25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]\\d|\\d)
(?:\\.(?:25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]\\d|\\d)){3}
`
    .replace(/\n/g, '')
    .trim()

const v6seg = '[a-fA-F\\d]{1,4}'

const v6reg = `
(?:
(?:${v6seg}:){7}(?:${v6seg}|:)|
(?:${v6seg}:){6}(?:${v4reg}|:${v6seg}|:)|
(?:${v6seg}:){5}(?::${v4reg}|(?::${v6seg}){1,2}|:)|
(?:${v6seg}:){4}(?:(?::${v6seg}){0,1}:${v4reg}|(?::${v6seg}){1,3}|:)|
(?:${v6seg}:){3}(?:(?::${v6seg}){0,2}:${v4reg}|(?::${v6seg}){1,4}|:)|
(?:${v6seg}:){2}(?:(?::${v6seg}){0,3}:${v4reg}|(?::${v6seg}){1,5}|:)|
(?:${v6seg}:){1}(?:(?::${v6seg}){0,4}:${v4reg}|(?::${v6seg}){1,6}|:)|
(?::(?:(?::${v6seg}){0,5}:${v4reg}|(?::${v6seg}){1,7}|:))
)(?:%[0-9a-zA-Z]{1,})?
`
    .replace(/\s*\/\/.*$/gm, '')
    .replace(/\n/g, '')
    .trim()

const ipreg = {
    v4: new RegExp(`^${v4reg}$`),
    v6: new RegExp(`^${v6reg}$`),
    v46: new RegExp(`(?:^${v4reg}$)|(?:^${v6reg}$)`),
    v4c: new RegExp(v4reg, 'g'),
    v6c: new RegExp(v6reg, 'g'),
    v46c: new RegExp(`(?:${v4reg})|(?:${v6reg})`, 'g'),
}

export default {
    isIP: (addr, find = false) => (find ? ipreg.v46 : ipreg.v46c).test(addr),
    isIPv4: (addr, find = false) => (find ? ipreg.v4 : ipreg.v4c).test(addr),
    isIPv6: (addr, find = false) => (find ? ipreg.v6 : ipreg.v6c).test(addr),
    version: str => (ipreg.v4c.test(str) ? 4 : ipreg.v6c.test(str) ? 6 : -1),
    extract: str => str.match(ipreg.v46c),
    extract_int: str => {
        if (ipreg.v4c.test(str))
            return str
                .match(ipreg.v4c)[0]
                .split('.')
                .map(_ => (_ ? parseInt(_) : 0))
        if (ipreg.v6c.test(str))
            return str
                .match(ipreg.v6c)[0]
                .split(':')
                .map(_ => (_ ? parseInt(_, 16) : 0))
        return []
    },
}
