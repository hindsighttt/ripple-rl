import json,urllib.request,websocket,sys
port=sys.argv[1]; expr=sys.argv[2]
t=[x for x in json.load(urllib.request.urlopen(f'http://127.0.0.1:{port}/json')) if x.get('type')=='page'][0]
ws=websocket.create_connection(t['webSocketDebuggerUrl'],timeout=30)
ws.send(json.dumps({'id':1,'method':'Runtime.evaluate','params':{'expression':expr,'awaitPromise':True,'returnByValue':True}}))
while True:
    m=json.loads(ws.recv())
    if m.get('id')==1: print(json.dumps(m,ensure_ascii=False)[:3000]); break
