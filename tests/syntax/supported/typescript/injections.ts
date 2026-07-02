/** @returns {Promise<string>} rendered markup */
function render(): string {
  const pattern = /ab+c/gi;
  return html`<strong>${pattern.source}</strong>`;
}

const styles = css`body { color: red; }`;
