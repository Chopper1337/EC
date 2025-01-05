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

Change `cl_crosshairalpha` in game to switch the config.

I have created a few usable configurations.



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

240 to 245 are the same settings are 250 to 255 but with visuals always enabled.

230 and 231 are basically semi rage.
