import assert from 'node:assert/strict';
import net from 'node:net';
import test from 'node:test';
import { checkBridge } from './check-bench-bridge.mjs';
async function mock(handler,run) {
  const server=net.createServer(handler);await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  try {await run(server.address().port);} finally {await new Promise(resolve=>server.close(resolve));}
}
test('read-only bench check, captured prelude and BE PLL read, fragmented response',async()=>{
  await mock(socket=>socket.once('data',bytes=>{
    assert.equal(bytes.toString('hex'),'1b004006000101010000010a0000000e700000000af0000000');
    const reply=Buffer.from('0b00000018700000000af000000000600002000100010001','hex');
    socket.write(reply.subarray(0,7));setImmediate(()=>socket.end(reply.subarray(7)));
  }),async port=>assert.deepEqual(await checkBridge('127.0.0.1',port),[0x60,2,1,1,1]));
});
test('PLL unlock and invalid response are not reported as success',async()=>{
  for(const hex of ['0b00000018700000000af000000000600002000100010000','0b00000018700000000a00f0000000600002000100010001']) {
    await mock(socket=>socket.once('data',()=>socket.end(Buffer.from(hex,'hex'))),async port=>{
      await assert.rejects(checkBridge('127.0.0.1',port));
    });
  }
});
test('early close is terminal',async()=>{
  await mock(socket=>socket.once('data',()=>socket.end(Buffer.from('0b00','hex'))),async port=>{
    await assert.rejects(checkBridge('127.0.0.1',port),/closed after 2/);
  });
});
