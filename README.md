# FUOTA sample application

## Environment

The code provided by this repository is based on [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32/get-started/index.html).

It is assumed that the repository is cloned into `~/Dev` directory.

Important: the code was written for ESP-IDF version 4.4.4. It has not been tested on version 5.0.

The application should run on any ESP32 board. It is assumed that the board has 4 MB of flash memory.

The present README file assumes that the reader:
* Knows how to configure and build an ESP-IDF application
* Has basic knowledge of network concepts (DHCP, fixed IP addresses, etc.)

## Overview

This repository provides a component implementing a simple mechanism to perform Firmware Update Over The Air (FUOTA) for the ESP32, and a sample application presenting one possible way to use the component.

It must be used with the FUOTA server provided by [`docker-fuota-server`](https://github.com/PascalBod/docker-fuota-server) repository.

The update is performed in two steps:
* The device first provides its identity and its application version, and the server returns the name of the  binary update file, if an update of the application is available
* If an update is available, the device then requests it

## Components

The update process is implemented by the *fuota_b* component. This component assumes that IP connectivity is available. Another component, *conn_wifi_b* is used to set up this connectivity, by connecting to an adequate Wi-FI Access Point (AP). Finally, a third component, *scan_wifi_b* is used to look for this AP.

The `b` suffix letter used in the name of each component means *blocking*: the functions implementing the API of these components do not return until they have done their job (scanning available APs, connecting to an AP, etc.)

Each component API is described in the associated header file.

## Sample application

Before being built, the application must be configured with the name and the password of the AP that will be used for the updates. This AP is named *FUOTA AP* in what follows. The configuration is performed with *menuconfig* (see farther below).

The application must also be configured with the information allowing it to connect to the update server (see farther below).

When it starts, the application first waits for some time. Then it scans available Wi-Fi APs. If the configured FUOTA AP is present, the application connects to it, and then requests the update. After a successful update, it restarts.

## Application design

### FUOTA

#### How to trigger an update?

When considering the implementation of an OTA firmware update function, there are two possible ways:
* The server informs the device that an update is available
* The device checks whether an update is available by contacting the server on a periodic basis

The first way requires a permanent connectivity. The second way does not require it, and consequently addresses more use cases. The present application implements a solution conforming to the second way (it was initially designed for a use case where the ESP32 was installed in a moving vehicle).

More precisely, the application performs the following actions:
* On a periodic basis, it checks the available Wi-Fi APs (using *scan_wifi_b* component)
* If one of the APs is a FUOTA AP, the application connects to it (using *conn_wifi_b* component)
* Then, if a new firmware is available on the server, it performs the update (using *fuota_b* component)

#### Application download and storing

To perform an update, the *fuota_b* component uses the simple interface provided by [`esp_https_ota`](https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32/api-reference/system/esp_https_ota.html).

This ESP-IDF function performs the various required steps:
* It downloads the new application, and stores it into a dedicated flash memory area
* It checks that the downloaded file is a bootable application
* It records the fact that the ESP32 has to start the new application on next reboot

#### OTA partitions

A [specific partition scheme](https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32/api-reference/system/ota.html?highlight=ota#ota-data-partition) is required. A default OTA partition scheme is provided by ESP-IDF. On our side, we chose to use our own one, in order to remove the factory partition, thus providing more space to each of the two OTA partitions.

This partition file is `fuota_partitions.csv`.

#### Server certificate

The update server is identified with a certificate. The certificate contains the domain name (or the IP address) of the server and its public key, and is signed by the server's private key. Thanks to the certificate, the device can check that the server it contacts is the real one. And the certificate allows to encrypt the communication between the device and the server.

The certificate must be copied into the `server_certs` directory and the name of the file must be `ca_cert.pem`. 

The certificate is made available to the application thanks to two declarations:
* The `main/CMakeLists.txt` file passes the `EMBED_TXTFILES` argument to the component registration function
* An `extern` declaration, in `main.c`, references the start of the file contents put into flash memory by the build process thanks to the above argument

See farther below one way to generate the certificate.

### Blocking components

In ESP-IDF, the interface provided for the connection to a Wi-Fi AP is non blocking. Success or failure of the connection is reported by an asynchronous event, which can be intercepted by an event handler declared by the application. Scanning available Wi-Fi APs, for its part, can be either a blocking or a non-blocking operation.

In order to provide a consistent type of interface, easy to integrate into an application, the three components provided by this project (*scan_wifi_b*, *conn_wifi_b* and *fuota_b*) have a blocking interface. More precisely, the components are implemented in the following way:
* *Scan_wifi_b* and *fuota_b* components only calls ESP-IDF blocking functions
* *Conn_wifi_b* component relies on a dedicated event handler, and a task which processes the events. The synchronization between the component task and the calling task is done thanks to a binary semaphore. Information to be returned to the calling task is passed using a shared variable

### *Conn_wifi_b* component

A diagram describing the Finite State Machine implemented by the *conn_wifi_b* component can be found in `doc` directory.

## How to build, install and test the whole system

### ESP32 application and server application

The whole system is made of the present ESP32 application and of the [server application](https://github.com/PascalBod/docker-fuota-server).

### Local network configuration

To test the system in local mode, i.e. without having to host the server application on the Internet, you must ensure that the computer where the server application will run and the ESP32 use the same Wi-Fi AP.

If possible, configure the AP so that its DHCP server provides the computer with a fixed IP addresse. In the sections below, we assume that the resulting network configuration will be:

```
              +----------+
              | Wi-Fi AP | SSID: fuota_ap
              |          | password: fuota_pw
              +----------+
+-------+       |      |       +----------+
| ESP32 |-------+      +-------| Computer |
+-------+                      +----------+
                               192.168.1.41
```

### The server

#### Installation

Ensure that Python 3.7 minimum is installed. Additionally, install *curl* and *openssl*.

Get the [server](https://github.com/PascalBod/docker-fuota-server).

Create the directory where you will run the server. In what follows, it is assumed that this directory is `~/fuota_server/app`.

Copy the `docker-fuota-server/docker/server.py` file to `~/fuota_server/app` directory.

Create the `~/fuota_server/data` directory.

Create a self-signed certificate for the server:
```bash
$ cd ~/fuota_server/app
$ openssl req -x509 -newkey rsa:2048 -keyout ca_key.pem -out ca_cert.pem -days 365 -nodes
```

When prompted for the **Common Name**, enter the IP address of the server (192.168.1.41).

Choose a username and a password for the server. In what follows, it is assumed that the username is `server_username` and the password is `server_password`.

#### Test

Start the server:
```bash
$ cd ~/fuota_server/app
$ US_AUTH_USERNAME=server_username US_AUTH_PASSWORD=server_password US_SERVER_PORT=50000 python server.py
```

From another terminal, you can check that the server is active:
```bash
$ curl -u server_username:server_password \
       --cacert $HOME/fuota_server/app/ca_cert.pem \
       --verbose --request GET 'https://192.168.1.41:50000/devices'
```

This command should display several log messages, ending with something similar to:
```
< HTTP/1.0 200 OK
< Server: BaseHTTP/0.6 Python/3.8.10
< Date: Mon, 22 May 2023 12:23:35 GMT
< Content-type: text/csv
< Content-disposition: attachment; filename="/tmp/tmpbdcjs__m"
< 
* Closing connection 0
* TLSv1.3 (OUT), TLS alert, close notify (256):
```

For its part, the server terminal should display log messages similar to these ones:
```
----------------------------------------
GET request from 192.168.1.41:59508
----------
Headers:
Host: 192.168.1.41:50000
Authorization: Basic c2VydmVyX3VzZXJuYW1lOnNlcnZlcl9wYXNzd29yZA==
User-Agent: curl/7.68.0
Accept: */*


----------
Request: GET /devices HTTP/1.1
192.168.1.41 - - [22/May/2023 14:23:35] "GET /devices HTTP/1.1" 200 -
```

For more information about the server API, check the [server repository](https://github.com/PascalBod/docker-fuota-server).

### The ESP32

#### Build and installation

Copy the `ca_cert.pem` files generated for the server into the `esp32-fuota/server_certs` directory.

Using *menuconfig*, sets the following configuration values, in **Component config > esp32-fuota configuration**:
* **SSID of the OTA update AP** (`fuota_ap` in our case)
* **Password of the OTA update AP** (`fuota_pw` in our case)
* **Domain name of the update server** (`192.168.1.41` in our case)
* **Update server port** (`50000` in our case)
* **Update server username** (`server_username` in our case)
* **Update server password** (`server_password` in our case)

In addition to these values, defined by the application, you have to set three other ones:
* **Serial flasher config > Flash size** must be set to **4 MB**
* **Component config > PHY > Use a partition to store PHY init data** must not be checked
* **Partition Table** must be configured as follows:
  * **Partition Table : Custom partition table CSV**
  * **Custom partition CSV file** : `fuota_partitions.csv`

Build the application, flash the ESP32.

Note: the ESP32 application needs a unique identifier in order to let the server know which ESP32 board is talking to it. The `DEV_ID` constant, defined in `main.c`, is used for this purpose. If you want to test the OTA update with several ESP32 boards, do not forget to modify this constant for each of them. I agree, this value could be moved to the configuration menu :-) 

#### Test of the connection

After a short wait period, the ESP32 application will try to connect to the Wi-FI AP and then to the server application. IDF monitor should display log messages similar to these ones:
```
I (72357) OTA: Starting update with 192.168.1.41:50000
I (74477) OTA: HTTP_EVENT_ON_CONNECTED
I (74487) OTA: HTTP_EVENT_HEADERS_SENT
I (74587) OTA: HTTP_EVENT_ON_HEADER, key=Server, value=BaseHTTP/0.6 Python/3.8.10
I (74587) OTA: HTTP_EVENT_ON_HEADER, key=Date, value=Mon, 22 May 2023 13:32:34 GMT
I (74587) OTA: Content length: 0
W (74587) OTA: Not Found
I (74597) OTA: HTTP_EVENT_DISCONNECTED - data length: 0
I (74607) OTA: HTTP_EVENT_DISCONNECTED - data length: 0
I (74607) APP: No update available
I (74607) CWB: Sendind disconnection request to task
I (74617) CWB: WAIT_DIS_CMD - Disconnection request
```

The *No update available* message is normal: no update file has been provided to the server. In the next sections we will generate a new version, upload it to the server, declare the update availability and check that the ESP32 gets it.

## Delivering updates

### Creating a new version

If you checked the log messages written by the ESP32 application, you saw that one of the first messages is `===== esp32-fuota 0.1.0 =====`.

Create a new version of the application, by modifying the version number defined by the `OTA_VERSION` constant, setting it to `0.1.1` for instance.

Build the application. The resulting binary file is `esp32-fuota/build/esp32-fuota.bin`.

### Uploading the update file

Now, the new binary file has to be uploaded to the update server. This is done thanks to a command similar to:
```bash
$ curl -u server_username:server_password \
       --cacert $HOME/fuota_server/app/ca_cert.pem \
       --verbose --request PUT --data-binary @$HOME/Dev/esp32-fuota/build/esp32-fuota.bin \
       --header "Content-Type: application/octet-stream" \
       'https://192.168.1.41:50000/files/esp32-fuota.bin.0.1.1'
```

Check that the server log messages are similar to these ones:
```
----------------------------------------
PUT request from 192.168.1.41:44158
----------
Headers:
Host: 192.168.1.41:50000
Authorization: Basic c2VydmVyX3VzZXJuYW1lOnNlcnZlcl9wYXNzd29yZA==
User-Agent: curl/7.68.0
Accept: */*
Content-Type: application/octet-stream
Content-Length: 816384
Expect: 100-continue


----------
Request: PUT /files/esp32-fuota.bin.0.1.1 HTTP/1.1
Expected length: 816384
End of reception
192.168.1.41 - - [22/May/2023 15:56:24] "PUT /files/esp32-fuota.bin.0.1.1 HTTP/1.1" 200 -
```

### Setting the update information for the ESP32 board

Now, it's time to declare the availability of the update file for your ESP32 board. 

Use a command similar to this one to declare that an update file is available:
```bash
$ curl -u server_username:server_password \
       --cacert $HOME/fuota_server/app/ca_cert.pem \
       --verbose --request PUT --data '"00001","0.1.0","esp32-fuota.bin.0.1.1"' \
       --header "Content-Type: text/csv" \
       'https://192.168.1.41:50000/devices/00001'
```

`00001` is the ESP32 unique identifier defined by the `DEV_ID` constant. `0.1.0` is the current version of the ESP32 application.

At next check for an update, IDF monitor should display log messages similar to the following ones:
```
I (35296) OTA: Starting update with 192.168.1.41:50000
I (36276) OTA: HTTP_EVENT_ON_CONNECTED
I (36286) OTA: HTTP_EVENT_HEADERS_SENT
I (36296) OTA: HTTP_EVENT_ON_HEADER, key=Server, value=BaseHTTP/0.6 Python/3.8.10
I (36296) OTA: HTTP_EVENT_ON_HEADER, key=Date, value=Mon, 22 May 2023 14:28:34 GMT
I (36306) OTA: HTTP_EVENT_ON_HEADER, key=Content-Type, value=text/csv
I (36316) OTA: HTTP_EVENT_ON_HEADER, key=Content-Length, value=21
I (36316) OTA: Content length: 21
I (36326) OTA: OK
I (36326) OTA: HTTP_EVENT_DISCONNECTED - data length: 21
I (36336) OTA: HTTP_EVENT_DISCONNECTED - data length: 21
I (36336) OTA: Requesting esp32-fuota.bin.0.1.1
I (37036) OTA: HTTP_EVENT_ON_CONNECTED
I (37036) OTA: HTTP_EVENT_HEADERS_SENT
I (37056) OTA: HTTP_EVENT_ON_HEADER, key=Server, value=BaseHTTP/0.6 Python/3.8.10
I (37056) OTA: HTTP_EVENT_ON_HEADER, key=Date, value=Mon, 22 May 2023 14:28:34 GMT
I (37066) OTA: HTTP_EVENT_ON_HEADER, key=Content-type, value=application/octet-stream
I (37066) OTA: HTTP_EVENT_ON_HEADER, key=Content-Disposition, value=attachment; filename="../data/esp32-fuota.bin.0.1.1"
I (37086) esp_https_ota: Starting OTA...
I (37086) esp_https_ota: Writing to partition subtype 16 at offset 0x10000
I (59366) wifi:<ba-add>idx:1 (ifx:0, 74:9d:79:79:54:b4), tid:4, ssn:0, winSize:64
I (65396) esp_image: segment 0: paddr=00010020 vaddr=3f400020 size=1c604h (116228) map
I (65436) esp_image: segment 1: paddr=0002c62c vaddr=3ffb0000 size=038e0h ( 14560) 
I (65446) esp_image: segment 2: paddr=0002ff14 vaddr=40080000 size=00104h (   260) 
I (65446) esp_image: segment 3: paddr=00030020 vaddr=400d0020 size=92284h (598660) map
I (65646) esp_image: segment 4: paddr=000c22ac vaddr=40080104 size=15228h ( 86568) 
I (65676) OTA: HTTP_EVENT_DISCONNECTED - data length: 816405
I (65686) OTA: HTTP_EVENT_DISCONNECTED - data length: 816405
I (65686) esp_image: segment 0: paddr=00010020 vaddr=3f400020 size=1c604h (116228) map
I (65726) esp_image: segment 1: paddr=0002c62c vaddr=3ffb0000 size=038e0h ( 14560) 
I (65736) esp_image: segment 2: paddr=0002ff14 vaddr=40080000 size=00104h (   260) 
I (65736) esp_image: segment 3: paddr=00030020 vaddr=400d0020 size=92284h (598660) map
I (65936) esp_image: segment 4: paddr=000c22ac vaddr=40080104 size=15228h ( 86568) 
I (66026) OTA: Update successful
I (66026) APP: Firmware updated, restarting
I (66026) CWB: Sendind disconnection request to task
I (66026) CWB: WAIT_DIS_CMD - Disconnection request
```

The ESP32 should reboot, and then display the new application version: ` ===== esp32-fuota 0.1.1 =====`.

Starting from now, every check for a possible new update will return a `No update available` status.

### Docker image

Once you have tested the system using your development computer, you may want to install the server application on a computer accessible from the Internet, so that your devices can request firmware updates wherever they are, as long as they have access to a Wi-Fi AP providing access to the Internet.

The [server repository](https://github.com/PascalBod/docker-fuota-server) provides a Docker image, which allows an easy installation. Refer to the repository's README file for more information.