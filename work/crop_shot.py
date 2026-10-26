from PIL import Image

im = Image.open(r"X:\coding\DeepGlow\smoke_out\user_shot.png").convert("RGB")
crop = im.crop((230, 180, 700, 320))
crop = crop.resize((crop.width * 2, crop.height * 2), Image.NEAREST)
crop.save(r"X:\coding\DeepGlow\smoke_out\user_crop.png")
print(crop.size)

# also a contrast-stretched version around the checker level
import numpy as np
a = np.asarray(im).astype(np.float32)
b = np.clip((a - 40.0) / (90.0 - 40.0), 0, 1) * 255
Image.fromarray(b.astype(np.uint8)).save(r"X:\coding\DeepGlow\smoke_out\user_stretch.png")
print("stretch written")
