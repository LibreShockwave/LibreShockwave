# Go To Private Room Harness Flow

Use this flow for future `venus-quackster-harness` tests that need to enter the
private room named `test`.

## URL

```text
http://localhost:3000/venus-quackster-harness
```

## Manual Flow

1. Open the harness URL.
2. Wait about 60 seconds for login, asset loading, and the Navigator to settle.
3. In the Navigator, click the `Rooms` tab.
4. In `Recommended Rooms`, select/use the `test` row.
5. Click `Go`.
6. Confirm the private room loads. The expected room has:
   - room name `test`
   - owner `Quackster`
   - area sofa on the left
   - `rare_dragonlamp` on the right

## Selenium Canvas Flow

The harness is canvas-rendered, so DOM text locators usually are not useful.
Use direct canvas mouse events with top-left canvas coordinates.

Known working coordinates from a `1200x764` Firefox window:

```text
Rooms tab: (845, 65)
Go button: (881, 157)
```

Example click helper:

```python
def click_canvas(driver, canvas, x, y):
    driver.execute_script("""
        const canvas = arguments[0];
        const x = arguments[1], y = arguments[2];
        const r = canvas.getBoundingClientRect();
        const cx = r.left + x, cy = r.top + y;
        for (const type of ['pointerdown','mousedown','pointerup','mouseup','click']) {
            const ev = type.startsWith('pointer')
              ? new PointerEvent(type, {
                  bubbles: true, cancelable: true, pointerId: 1,
                  pointerType: 'mouse', isPrimary: true,
                  clientX: cx, clientY: cy, button: 0,
                  buttons: type.endsWith('down') ? 1 : 0
                })
              : new MouseEvent(type, {
                  bubbles: true, cancelable: true,
                  clientX: cx, clientY: cy, button: 0,
                  buttons: type.endsWith('down') ? 1 : 0
                });
            canvas.dispatchEvent(ev);
        }
    """, canvas, x, y)
```

Minimal sequence:

```python
driver.get("http://localhost:3000/venus-quackster-harness")
time.sleep(60)
canvas = driver.find_element(By.TAG_NAME, "canvas")
click_canvas(driver, canvas, 845, 65)
time.sleep(2)
click_canvas(driver, canvas, 881, 157)
time.sleep(14)
driver.save_screenshot("/tmp/private_room.png")
```

## Notes

- `ActionChains.move_to_element_with_offset` may treat offsets relative to the
  canvas center in this setup and can produce out-of-bounds clicks. Prefer the
  direct JavaScript canvas event helper above.
- If the `Go` click leaves the Navigator open on the hotel exterior, the click
  was too high. Use the lower `Go` coordinate around `(881, 157)`.
- For visual renderer tests, take a screenshot after room load before making or
  validating code changes.
