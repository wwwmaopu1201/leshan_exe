import test from 'node:test'
import assert from 'node:assert/strict'
import { parseEmployeeRows, decodeEmployeeFile } from '../src/utils/employee-import.js'
import { isServerHost, readLoginHistory, saveLoginHistory } from '../src/utils/login-history.js'

test('CSV parses quoted commas, multiline remarks and pasted tabs', () => {
  assert.deepEqual(parseEmployeeRows('\ufeff工号,姓名,电话,备注\r\n001,"李,明",137 8120 0213,"一行\n二行"'), [
    ['工号','姓名','电话','备注'], ['001','李,明','137 8120 0213','一行\n二行']
  ])
  assert.deepEqual(parseEmployeeRows('001\t张三\t13800000000\t说明'), [['001','张三','13800000000','说明']])
  assert.throws(() => parseEmployeeRows('001,"unclosed'), /引号/)
})

test('CSV decodes UTF8, UTF16 and GBK', () => {
  assert.equal(decodeEmployeeFile(new TextEncoder().encode('员工').buffer),'员工')
  assert.equal(decodeEmployeeFile(new Uint8Array([0xff,0xfe,0x58,0x54,0xe5,0x5d]).buffer),'员工')
  assert.equal(decodeEmployeeFile(new Uint8Array([0xd4,0xb1,0xb9,0xa4]).buffer),'员工')
})

test('server validation separates malformed IP from valid unreachable addresses', () => {
  for (const host of ['127.0.0.1','122.51.79.128','localhost','server-1.local']) assert.equal(isServerHost(host),true,host)
  for (const host of ['122. 51.79.127','256.1.1.1','1.2.3','http://localhost','127.0.0.1:8088','']) assert.equal(isServerHost(host),false,host)
})

test('remembered credentials are scoped to both server and account', () => {
  const data = new Map()
  globalThis.localStorage = {getItem: key => data.get(key),setItem: (key,value) => data.set(key,value)}
  saveLoginHistory({serverIp:'one',username:'a',password:'first',remember:true})
  saveLoginHistory({serverIp:'two',username:'a',password:'second',remember:true})
  saveLoginHistory({serverIp:'one',username:'a',password:'new',remember:false})
  assert.equal(readLoginHistory().find(row => row.serverIp==='one').password,'')
  assert.equal(readLoginHistory().find(row => row.serverIp==='two').password,'second')
})
