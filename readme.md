# install
```
sudo apt install libstrophe-dev libssl-dev jq
```

# compile
```
gcc -o xmpp2bash xmpp2bash.c -lstrophe -lssl -lcrypto
```


# run
```
./xmppbot your_jid@jabberix.com your_secure_password
```