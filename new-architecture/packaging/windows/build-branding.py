"""Create NSIS BMP artwork from the existing application logos (requires Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw
root = Path(__file__).resolve().parents[2]
out = Path(__file__).resolve().parent/'branding'
out.mkdir(exist_ok=True)
for product, filename in [('client','client/assets/client-app-icon.png'), ('server','server/assets/app-icon.png')]:
    logo = Image.open(root/filename).convert('RGBA')
    logo = logo.crop(logo.getbbox())
    welcome = Image.new('RGB',(164,314),'#f1f6ff')
    draw = ImageDraw.Draw(welcome)
    draw.rectangle((0,0,163,5),fill='#2f86ee')
    draw.rectangle((0,308,163,313),fill='#2f86ee')
    icon = logo.copy(); icon.thumbnail((112,112),Image.Resampling.LANCZOS)
    welcome.paste(icon,((164-icon.width)//2,82),icon)
    welcome.save(out/f'{product}-welcome.bmp')
    header = Image.new('RGB',(150,57),'white')
    icon = logo.copy(); icon.thumbnail((44,44),Image.Resampling.LANCZOS)
    header.paste(icon,(150-icon.width-8,(57-icon.height)//2),icon)
    header.save(out/f'{product}-header.bmp')
