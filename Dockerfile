FROM i386/ubuntu

RUN apt update
RUN apt install -y build-essential git qemu-system-x86

WORKDIR /src

CMD make clean && /bin/bash
