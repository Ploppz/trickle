# Trickle
The goal of the trickle algorithm is to distribute the latest version of some value/data.

_Instance_ in this text means a running instance of a trickle algorithm. It has an associated ticker for scheduling events, a version number, a value and a (possibly implicit) key.

## Getting started
Open `positioning.emProject` in Segger Embedded Studio. After building (`F7`), you can use `flash.py` (in linux: `./flash.py positioning`) to flash all boards that are connected with USB. Start one board in debug mode by pressing `F5` and then press `F5` to run. The local state will be written to a debug window in Segger Embedded Studio every 5 second.

`main.c` uses an application from the `app` directory based on the preprocessor definition `APP_NAME`, which is "positioning" in the positioning emProject. The `toggle` project is an experiment that is not very useful (and not finished or maintained, an artifact) but a good starting point for an application which has one instance per node, for example a network for measuring temperature.

# Overview
### Features
* Instances are identified by variable-size keys -> there is _logically_ no limit to the number of instances (disregarding the Ticker limitations), and it is convenient for identifying instances.
* 98% radio uptime (tested with [this](https://github.com/JarlV/Mesh-Testing)).
### Limitations
* Ticker time slots are not used, so `trickle` can't reasonably be used in conjunction with other apps that use the radio. This ought to change.
* Two ticker timers are used per trickle instance. Hence there cannot be any more than 127 trickle instances. In the future, Ticker timers should not be used for this.
* There is no mechanism to push out inactive instances. Like for example the cache as in [bcast-mesh](https://github.com/NordicSemiconductor/nRF51-ble-bcast-mesh).

# Architecture
## Key-value solution
If a node has _exclusive write access_ to a trickle instance, it is meant that only this node updates the value of the instance. This will not be imposed, but rather facilitated in the code. The key-value solution implemented in this repository is one way to do this.

In the key-value solution, an instance is identified by a **key** rather than a number. A key can have a variable length, as can a value. This is facilitated with the following packet structure (bold is number of bytes).

 4       | 1        | key\_len | 1        | val\_len 
 --------|----------|----------|----------| ---
 version | key\_len | key      | val\_len | val     

Some functionality is delegated to the application itself. It's up to the application to implement the following mappings:

```
     key -> instance
instance -> key
instance -> val
```

Furthermore, it's up to the application to scan for packets and pass them to trickle for registration and appropriate action.

The application allocates all the memory for instances and values. As for the `key -> instance` lookup, the application will typically not allocate space for all the keys, but employ a more memory efficient, domain specific access structure. The positioning application is a good example of how we only need to store a maximum of `N * 6` bytes for key lookup despite a total of `N^2` keys, each of length `12` (disregarding the `app_id`, shortly to be discussed).

Using the positioning application as an illustrative example, keys have the following structure.

 2      | 6         | 6         
--------|-----------|-----------
 app\_id | dev\_addr1 | dev\_addr2 

The `app_id` is constant for the positioning application and used to discard messages that aren't relevant. An application running several trickle applications could also use `app_id` to demultiplex a trickle packet to the correct trickle application. This behavior is easily customizable, since the mapping from keys to trickle instances is done by the application. The device addresses (`dev_addrX`) are 6 bytes and unique for each device/node.

## `rio` - radio in/out
Due to problems with Ticker, we tried to limit the number of tickers in use. PhoenixLL's scanning module required two tickers for transmission per trickle instance, due to the way the scanner uses time outside its time slot.

`rio` is a module that does radio exclusively, with only one ticker in total, using no Ticker timeslots. It has circular buffers `inbox` and `outbox` for packets received and to be transmitted, respectively. By default it is scanning for packets, but starts transmitting when it needs to.

It would be advantageous to use Ticker slots, which was a big part of the rationale behind this project. However, with the short timeframe of this project, we did not find time to fully address the Ticker problems encountered. `rio` only limits the problem, making the product usable for now.

We have gone to some length to ensure context safety, but not sure whether it's context safe at the moment - we haven't encountered any problems with it yet. Main contex pops `inbox` and pushes `outbox`, while ISR context pops `outbox` and pushes `inbox`. Main and ISR context mutates a disjoint set of shared variables.

## Diagrams
### Block diagram
![block](http://i.imgur.com/dehe5LO.png)

### Reception
![rx](http://i.imgur.com/2P9RIu6.png)

### Transmission
![tx](http://i.imgur.com/sDrhnJF.png)

# Current Ticker problems
The Ticker problems before `rio` were mostly `ticker_start` and `ticker_update` returning `TICKER_STATUS_FAILURE`, but we also got a wide variety of strange errors. Allocating memory for 30 user ops didn't eliminate the problems.

Even without slots (using `rio`), the Ticker errors aren't eliminated. At the moment, there are 2 tickers per trickle instance. These are needed solely for keeping track of events of the trickle algorithm. Seeing as trickle instances are only started when their value is first written to (from external packet, or internally), the number of `ticker_start`s depends on the number of instances that are _active_ in the network. The number of instances active is the most prominent factor of failure. When testing the positioning application, it is impossible to have 5 nodes running at the same time (at least one will crash immediately), and 4 nodes is dubious. 3 nodes works fine for the most part. (Note that there are `(N^2 - N + 1)` number of instances when there are N nodes)

In summary, the more tickers are running at the same time, the more prone to failure.

Perhaps the problems we have faced can illuminate some problems with Ticker - unless of course it is all due to programming errors in `trickle`.
