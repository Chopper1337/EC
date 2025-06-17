# EC
open-source EC

## Usage

```
git clone https://github.com/Chopper1337/EC.git --recursive && \
./EC/projects/LINUX/run
```

Be sure to have a performant compositor running such as [fastcompmgr](https://github.com/tycho-kirchner/fastcompmgr) or the overlay will not be transparent.
(See [here](https://github.com/tycho-kirchner/fastcompmgr?tab=readme-ov-file#benchmark))

## Configuration

Configuration is done by editing the `./cs2/shared/cs2features.cpp` file.

Change `cl_crosshairalpha` in game to switch between configurations listed in the table below.

| `cl_crosshairalpha` | Aim FOV | Aim smooth | Visuals |
| ------------- | -------------- | -------------- | -------------- |
| 255 | 0.6 | 80 | Disabled | 
| 254 | 1.0 | 20 | Disabled | 
| 253 | 1 | 10 | Disabled | 
| 252 | 2.5 | 10 | Disabled | 
| 251 | 5.0 | 10 | Disabled | 
| 250 | 5.0 | 5 | Disabled | 
| 245 | 0.6 | 80 | Enabled | 
| 244 | 1.0 | 20 | Enabled | 
| 243 | 1 | 10 | Enabled | 
| 242 | 2.5 | 10 | Enabled | 
| 241 | 5.0 | 10 | Enabled | 
| 240 | 5.0 | 5 | Enabled | 
| 231 | 5.0 | 1.4 | Enabled | 
| 230 | 5 | 0 | Enabled | 

`cl_crosshairalpha` values 240 to 245 have visuals enabled, while 250 to 255 have them disabled.
The last digit (0 to 5) indicates aimbot strength, with lower values providing stronger aim assist.

230 and 231 should only be used if you are willing to risk a VAC Live cooldown or ban.

## Bindings

* Aimbot: Mouse 1
* Triggerbot: Mouse 4

You can use the in game binds system to toggle between configs:

`bind z "toggle cl_crosshairalpha 253 243"`

In this example, pressing `z` would essentially toggle visuals.

I'd suggest setting up numpad binds between 0 and 5 to toggle between configs in the 240-245 and 250-255 range.
