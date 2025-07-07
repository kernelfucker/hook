# hook
minimal screenshot utility

# compile
$ clang hook.c -o hook -Os -s -Wall -Werror -lxcb -lxcb-image -lxcb-randr -lz

# usage
$ ./hook

$ ./hook screenshot.png \# default is timestamp

$ ./hook -m

$ ./hook -d 2 -s

$ ./hook -d 2 -c

$ ./hook -f -s

# options
```
  -s    select region immediately
  -f    freeze screen when selecting, only with -s
  -c    countdown before screenshot
  -d    delay seconds before screenshot
  -m    screenshot current monitor only
```

# example
![image](https://github.com/user-attachments/assets/38c22ec2-faaa-4514-b852-2a4116850ebf)
