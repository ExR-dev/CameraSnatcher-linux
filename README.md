# CameraSnatcher-linux
School project: A program for linux written in C that reads camera data and scans for a red laser-dot.


Info:
    [Q]-[Y], [A]-[H], [Z]-[,] can be used to change settings.
    [Space] Captures and saves the current frame to a file.
    [Tab] prints the name and value of all settings.
    [Esc] closes out of the program.

    Pressing a key will print it's name and value.
    If it is a bool it will also switch states upon being pressed.
    If it is a float you can change it by pressing [Arr.Up]/[Arr.Down] while the key is held.

    These settings can also be set using command-line arguments following the format [name]=[float].
    Ex: ./release visualize=1 filter_hue=0.0 filter_sat=0.0 filter_val=0.0 scan_rad=1.2 skip_len=0
    
    
Settings:
    [Q] visualize: Visualize scan-strength of whole image, useful for configuring other settings.
    [W] greyscale: Visualize total strength as greyscale or individual hsv strengths as rgb (if visualize is also true).
    [E] verbose: Toggle verbose dot detection output.
    
    Threshold for skipping pixels based on distance from ideal color. 
    (Warning: lowering these can severely impact performance.)
    [R] filter_hue: ^
    [T] filter_sat: ^
    [Y] filter_val: ^
    
    [A] scan_rad: Radius of pixels surrounding the target pixel to scan.
    [S] skip_len: Amount of indices to skip after a valid pixel.
    [D] sample_step: The minimal distance between each pixel sampled within scan_rad.
    
    [F] dot_threshold: Minimum strength requirement for a pixel to be counted as a laser dot.
    [G] alt_weights: Interpolates between two methods of calculating HSV weights.
    
    HSV detection strength weights.
    [Z] h_str: ^
    [X] s_str: ^
    [C] v_str: ^
    
    Determines the impact of whites on the hue weights. 
    Used to penalize overexposed regions.
    [V] h_white_penalty: Maximum penalty for white regions.
    [B] h_white_falloff: The distance from pure white that penalizing starts.
    [N] h_white_curve: The exponent of the penalty multiplier.
    
    [M] compare_threading: Whether to run both single-threaded and multi-threaded code and compare performance.
    [,] thread_count: The amount of threads to use.

