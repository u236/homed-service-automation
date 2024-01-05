FROM    127.0.0.1:5000/homed-service:latest
ARG     TARGETARCH
RUN     apt update
RUN     apt install -y curl
RUN     mkdir /etc/homed
RUN     ln -s /data/homed-automation.conf /etc/homed/homed-automation.conf
RUN     ln -s /data/homed.log /var/log/homed.log
COPY    homed-automation-$TARGETARCH /usr/bin/homed-automation
CMD     ["/usr/bin/homed-automation"]
