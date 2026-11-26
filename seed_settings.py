import json, os
st=os.path.join(os.environ["APPDATA"],"RippleRL","settings.json")
d={
 "language":"en","theme":"dark","firstRunComplete":True,
 "library":{"cookedDir":"D:/SteamLibrary/steamapps/common/rocketleague\\TAGame\\CookedPCConsole","source":"Steam"},
 "target":{"cookedDir":"D:\\Epic Games Games\\rocketleague\\TAGame\\CookedPCConsole","source":"Epic"},
 "bringToFrontHotkey":"F2","alwaysOnTop":False,"closeToTray":True,"discordRpc":True,
 "startup":False,"backup":True,"proxyEnabled":True,"pseudo":""
}
open(st,"w",encoding="utf-8").write(json.dumps(d,indent=1))
print(st)
