FROM    127.0.0.1:5000/homed-service:latest
ARG     TARGETARCH
RUN     rm /var/lib/dpkg/info/libc-bin.*
RUN     apt update
RUN     apt install -y curl locales
RUN     echo "en_US.UTF-8 UTF-8" > /etc/locale.gen
RUN     locale-gen
RUN     mkdir /etc/homed
RUN     ln -s /data/homed-automation.conf /etc/homed/homed-automation.conf
RUN     ln -s /data/homed.log /var/log/homed.log
COPY    homed-automation-$TARGETARCH /usr/bin/homed-automation
ENV     LANG=en_US.UTF-8
CMD     ["/usr/bin/homed-automation"]
