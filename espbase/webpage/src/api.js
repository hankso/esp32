import { type } from '@/utils'

import axios from 'axios'
import { basename, resolve, join } from 'path-browserify'

function request_compat(config) {
    let url = config.url
    if (config.params) {
        let char = url.includes('?') ? '&' : '?'
        url = `${url}${char}${new URLSearchParams(config.params)}`
    }
    if (config.data) config.body = config.data
    return fetch(join('/api', resolve(url)), config)
        .then(resp => {
            if (!resp.ok)
                throw new Error(`${resp.status}: ${resp.statusText}`)
            return resp
        })
}

const request = axios.create({
    baseURL: '/api/',
    timeout: 1000,
})

export function getConfig(opt={}) {
    return request(axios.mergeConfig(opt, {
        url: 'config'
    }))
}

export function setConfig(cfg, opt={}) {
    return request(axios.mergeConfig(opt, {
        url: 'config',
        method: 'POST',
        data: { json: cfg }
    }))
}

export function update() {
/* TODO POST update?reset&size=int */
}

export function listDir(path='', opt={}) {
    return request(axios.mergeConfig(opt, {
        url: 'edit',
        params: { list: path }
    }))
}

export function readFile(path, download=false, opt={}) {
    return request(axios.mergeConfig(opt, {
        url: 'edit',
        params: download ? { path } : { path, download }
    }))
}

export function createPath(path, isdir=true, opt={}) {
    return request(axios.mergeConfig(opt, {
        url: 'editc',
        method: 'PUT',
        params: { path, type: isdir ? 'dir' : 'file' }
    }))
}
export function deletePath(path, opt={}) {
    return request(axios.mergeConfig(opt, {
        url: 'editd',
        method: 'DELETE',
        params: { path }
    }))
}

export function uploadFile(filename, data, opt={}) {
    if (type(data) !== 'formdata') {
        let tmp = data
        if (type(tmp) === 'string') {
            tmp = new Blob([tmp], { type: 'text/plain' })
        } else if (!['file', 'blob'].includes(type(tmp))) {
            return Promise.reject('Invalid file content-type')
        }
        data = new FormData()
        data.append(filename, tmp, basename(filename))
    }
    return request(axios.mergeConfig(opt, {
        url: 'editu',
        method: 'POST',
        params: { overwrite: '' },
        data,
        timeout: 5000
    }))
}
