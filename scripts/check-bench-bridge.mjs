import net from 'node:net';
import { pathToFileURL } from 'node:url';

// Read-only connectivity check. Closing this session resets DSP in the bench
// image: run BEFORE opening the GUI, never while a download/tuning is active.
export async function checkBridge(host='chime.local', port=8086) {
  return new Promise((resolve,reject)=>{
    const socket=net.createConnection({host,port});
    let received=Buffer.alloc(0), done=false;
    const timer=setTimeout(()=>finish(new Error('Bridge connect/read timed out after 5s.')),5000);
    function finish(error,value) {
      if(done) return;done=true;clearTimeout(timer);socket.destroy();
      if(error) reject(error);else resolve(value);
    }
    socket.on('connect',()=>socket.write(Buffer.from('1b004006000101010000010a0000000e700000000af0000000','hex')));
    socket.on('data',bytes=>{
      received=Buffer.concat([received,bytes]);
      if(received.length>24) return finish(new Error('Unexpected response length.'));
      if(received.length<24) return;
      const header=Buffer.from('0b00000018700000000af0000000','hex');
      if(!received.subarray(0,14).equals(header)) return finish(new Error(`Invalid response: ${received.toString('hex')}`));
      const registers=Array.from({length:5},(_,i)=>received.readUInt16BE(14+i*2));
      if(registers[0]!==0x60 || registers[1]!==2 || registers[2]!==1 || registers[3]!==1 || !(registers[4]&1))
        return finish(new Error(`Unexpected PLL registers: ${registers.map(v=>v.toString(16)).join(',')}`));
      finish(null,registers);
    });
    socket.on('error',error=>finish(error));
    socket.on('close',()=>{if(!done) finish(new Error(`Bridge closed after ${received.length} response bytes.`));});
  });
}
if(process.argv[1] && import.meta.url===pathToFileURL(process.argv[1]).href) {
  if(process.argv.includes('--help')) {
    console.log('node scripts/check-bench-bridge.mjs [host=chime.local] [port=8086]\nRead-only PLL check. GUI must be disconnected; closing the check resets DSP.');
  } else {
    try {
      const port=Number(process.argv[3]??8086);
      if(!Number.isInteger(port)||port<1||port>65535) throw new Error('Invalid TCP port.');
      const regs=await checkBridge(process.argv[2]??'chime.local',port);
      console.log(`PASS TCP -> I2C PLL read: ${regs.map((v,i)=>`F00${i}=${v.toString(16).padStart(4,'0')}`).join(' ')}\nNOT a GUI/download/audio test. Session closed; DSP reset; now open SigmaStudio+.`);
    } catch(error) {console.error(`FAIL ${error.message}`);process.exitCode=1;}
  }
}
