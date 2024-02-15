import { type, gzip_compress } from '@/utils'

import axios from 'axios'
import { basename, resolve, join } from 'path-browserify'

function merge_compat(opt1, opt2) {
    return Object.assign(opt1, opt2)
}

function request_compat(config) {
    let url = config.url
    if (config.params) {
        let char = url.includes('?') ? '&' : '?'
        url = `${url}${char}${new URLSearchParams(config.params)}`
    }
    if (config.data) config.body = config.data
    return fetch(join('/api', resolve('/', url)), config).then(resp => {
        if (!resp.ok) throw new Error(`${resp.status}: ${resp.statusText}`)
        return resp
    })
}

const merge = axios.mergeConfig

export const instance = axios.create({
    baseURL: '/api/',
    timeout: 1000,
})

export function getAsset(url, opt = {}) {
    return instance(merge(opt, { url }))
}

export function isApmode() {
    return instance({ url: '/apmode' })
}

export function getConfig(opt = {}) {
    return instance(
        merge(opt, {
            url: 'config',
        })
    )
}

export function setConfig(cfg, opt = {}) {
    return instance(
        merge(opt, {
            url: 'config',
            method: 'POST',
            data: `json=${JSON.stringify(cfg)}`,
        })
    )
}

async function toFormData(data, name = 'data') {
    let filename
    let tmp = data
    switch (type(data)) {
        case 'file': break
        case 'blob': break
        case 'object':
            tmp = new Blob([JSON.stringify(tmp)], { type: 'application/json' })
            break
        case 'string':
            filename = basename(name)
            if (filename.endsWith('.gz')) {
                let bytes = await gzip_compress(tmp)
                tmp = new Blob([bytes], { type: 'application/gzip' })
            } else {
                tmp = new Blob([tmp], { type: 'text/plain' })
            }
            break
        case 'uint8array':
        case 'arraybuffer':
            filename = basename(name)
            tmp = new Blob([tmp], { type: 'application/octet-stream' })
            break
        case 'formdata': return Promise.resolve(data)
        default: return Promise.reject({message: `Invalid type ${type(data)}`})
    }
    data = new FormData()
    data.append(name, tmp, ...(filename ? [filename] : []))
    return Promise.resolve(data)
}

export function update(firmware, opt = {}) {
    return toFormData(firmware, 'update').then(data =>
        instance(
            merge(opt, {
                url: 'update',
                method: 'POST',
                timeout: 0,
                data,
            })
        )
    )
}

export function listDir(path = '', opt = {}) {
    return instance(
        merge(opt, {
            url: 'edit',
            params: { list: path },
        })
    )
}

export function readFile(path, download = false, opt = {}) {
    return instance(
        merge(opt, {
            url: 'edit',
            responseType: 'text', // raw file: do NOT parse to json
            params: download ? { path, download } : { path },
        })
    )
}

export function createPath(path, isdir = true, opt = {}) {
    return instance(
        merge(opt, {
            url: 'editc',
            method: 'PUT',
            params: { path, type: isdir ? 'dir' : 'file' },
        })
    )
}
export function deletePath(path, opt = {}) {
    return instance(
        merge(opt, {
            url: 'editd',
            method: 'DELETE',
            params: { path },
        })
    )
}

export function uploadFile(filename, file, opt = {}) {
    return toFormData(file, filename).then(data =>
        instance(
            merge(opt, {
                url: 'editu',
                method: 'POST',
                params: { overwrite: '' },
                timeout: 0,
                data,
            })
        )
    )
}
