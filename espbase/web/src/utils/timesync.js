// TimeSync Client implementation in JavaScript.
// See python package neuseno.driver.timesync for details of Time Sync protocol
// and Server/Client implementation in Python.
//
// Author: Hank <hankso1106@gmail.com>
// Time: 2020.10.28 17:05:10

import { pause, getMonotonic } from '@/utils'

const encoder = new TextEncoder()
const decoder = new TextDecoder()

export default class TimeSyncClient {
    constructor(recv, send) {
        if (recv !== undefined) this.recv = recv
        if (send !== undefined) this.send = send
        this.offset = 0
        this.buffinit = new Uint8Array(8 + 8) // timeinit + 8
        this.buffsync = new Uint8Array(8 + 8) // timesync + 8
        this.buffdone = new Uint8Array(8 + 8 + 8) // timedone + 16
        this.viewinit = new DataView(this.buffinit.buffer)
        this.viewsync = new DataView(this.buffsync.buffer)
        this.viewdone = new DataView(this.buffdone.buffer)
        encoder.encodeInto('timeinit', this.buffinit)
        encoder.encodeInto('timedone', this.buffdone)
    }

    async recv() {
        throw Error('not implemented')
    }
    async send() {
        throw Error('not implemented')
    }

    async sync(ack = true) {
        let tc1 = getMonotonic()
        this.viewinit.setFloat64(8, tc1, true) // pack timestamp as double
        this.send(this.buffinit)
        let msg = await this.recv()
        let tc2 = getMonotonic()
        if (decoder.decode(msg.slice(0, 8)) !== 'timesync') {
            throw new Error('Invalid response: ' + msg)
        } else {
            this.buffsync.set(msg)
        }
        let tserver = this.viewsync.getFloat64(8, true) // unpack timestamp
        this.offset = tserver - (tc1 + tc2) / 2
        if (ack) {
            this.viewdone.setFloat64(8, (getMonotonic() + tc2) / 2, true)
            this.viewdone.setFloat64(16, this.offset, true)
            this.send(this.buffdone)
        }
        return self.offset
    }

    async xsync(iters = 10, timeout = 5000) {
        let arr = []
        for (let i = iters; i; i--) {
            await pause(((Math.random() + 0.5) * timeout) / iters)
            try {
                arr.push(await this.sync(!i))
            } catch {
                // do nothing
            }
        }
        if (arr.length)
            this.offset = arr.reduce((a, c) => a + c, 0) / arr.length
        return this.offset
    }
}
