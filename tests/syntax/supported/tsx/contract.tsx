/** Renders a card. @param props card properties */
interface Props {
  title: string;
  active?: boolean;
  onSelect(value: string): void;
}

export function Card({ title, active, onSelect }: Props): JSX.Element {
  const label: string = active ? title : "Inactive";
  const choose = (): void => onSelect(label);

  return (
    <section className="card" data-active={active}>
      <Header title={label} />
      <button onClick={choose}>{label}</button>
      <svg:path strokeWidth="2" />
    </section>
  );
}
